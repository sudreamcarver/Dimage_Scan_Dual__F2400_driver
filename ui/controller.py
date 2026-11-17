"""Desktop operation controller. No Tk and no device I/O on the UI thread."""
import datetime,json,os,re,signal,subprocess,sys,threading,time,uuid,tempfile
from pathlib import Path
from native import load_profile, BINARY

APP_ROOT=Path(__file__).resolve().parents[1]
ROOT=Path(os.environ.get('F2400_DATA_DIR') or (str(APP_ROOT) if (APP_ROOT/'build/f2400').exists() else str(Path(os.environ.get('XDG_DATA_HOME',Path.home()/'.local/share'))/'f2400')))
FILM_TYPES={'35mm 彩色正片':'color-positive','35mm 黑白负片':'bw-negative','彩色 RAW（RGB）':'raw-color','单通道黑白 RAW（绿色通道）':'raw-bw','35mm 彩色负片':'color-negative','35mm 黑白正片（尚未接入）':'bw-positive'}


def validate_film(film):
    if film not in ('color-positive','bw-negative','raw','raw-color','raw-bw','color-negative'):raise ValueError('所选胶片类型尚未接通完整扫描流程；请勿使用彩色正片算法扫描负片。')


def read_json(path):
    try:return json.loads(Path(path).read_text())
    except (OSError,ValueError):return {}


def tail(path):
    try:
        with Path(path).open('rb') as f:
            f.seek(0,2);size=f.tell();f.seek(max(0,size-65536));return f.read().decode('utf-8',errors='replace')
    except OSError:return ''


def scan_progress(directory,ratio):
    directory=Path(directory);report=read_json(directory/'result.json')
    stage=(report.get('transitions') or ['idle'])[-1]
    names={'idle':'准备就绪','metering':'自动曝光测光','meter-complete-release-ready':'计算曝光',
           'exposure-selected':'正式扫描准备','scan-complete-release-ready':'图像处理与导出','verifying':'校验扫描数据'}
    text=names.get(stage,stage);fraction=0
    for name,offset,span,total in [('meter',0,.15,438 if report.get('film') in ('35mm bw negative','35mm BW RAW') else 440),('full',.15,.75,3504//ratio+(0 if report.get('film') in ('35mm bw negative','35mm BW RAW') else 16//ratio))]:
        log=tail(directory/f'{name}.log')
        rows=re.findall(r'FACTORY LINE row=(\d+)',log)
        if rows:
            row=int(rows[-1]);fraction=offset+span*row/total
            if row<total:text=f'{"测光" if name=="meter" else "扫描"}：{row} / {total} 行'
            elif 'release_ready=1' not in log:text='等待机构收尾'
        elif name=='full' and log:
            text='正式扫描：采集暗场与准备';fraction=.15
    if stage in ('scan-complete-release-ready','verifying'):fraction=.95
    if report.get('status')=='complete':fraction=1;text='扫描完成，数据校验通过'
    return text,min(1,fraction),report


def capture_ready(directory,name):
    log=tail(Path(directory)/f'{name}.log')
    final=log.rsplit('FINAL STATUS CDB:',1)
    return len(final)==2 and 'MECHANICAL release_ready=1' in final[0] and bool(re.search(r'^status=00 host=0000 driver=0000 residual=0 ',final[1],re.M))


def setup_failure(log):
    sense='sense: 70 00 09 00 00 00 00 0c 00 00 00 00 04 02 00 00 00 00 80 00'
    if sense in log.splitlines():
        return '扫描器初始化尚未完成。原厂提示：请取下片夹。取下后检查状态；若仍报此错，需要完成冷启动初始化，请勾选片夹已取下，再点击“冷启动初始化”。片夹被吸住时不要强拉。'
    return None


def scan_failure(directory):
    for name in ('full','meter'):
        diagnostic=setup_failure(tail(Path(directory)/f'{name}.log'))
        if diagnostic:return diagnostic
    report=read_json(Path(directory)/'result.json')
    return report.get('error') or '操作失败，请查看日志。'


class Operations:
    def __init__(self,repo=ROOT):
        self.repo=Path(repo);self.lock=threading.Lock();self.cancel_event=threading.Event()
        self.state=dict(active=False,kind=None,initialize_required=False,message='准备就绪',progress=0,error=None,directory=None,returncode=None)
        self.process=None;self.temporary_runs={}

    def snapshot(self):
        with self.lock:return dict(self.state)

    def update(self,**values):
        with self.lock:self.state.update(values)

    def cancel(self):
        with self.lock:
            if self.state['active'] and self.state['kind'] in ('scan','calibration','initialize'):
                self.cancel_event.set();self.state['message']='正在取消，等待驱动收尾…'

    def start(self,kind,output_root,profile=None,ratio=1,film="color-positive",save_images=True):
        if kind not in ('scan','calibration','initialize','status','release'):raise ValueError('未知操作')
        if kind=='scan':
            validate_film(film)
            if self.snapshot().get('initialize_required'):raise ValueError('请先取下片夹并完成冷启动初始化，再放回胶片扫描。')
        raw_mode=film in ('raw','raw-color','raw-bw')
        if not str(output_root).strip() and (kind!='scan' or save_images):raise ValueError('请选择保存目录')
        output_root=Path(output_root).expanduser().resolve()
        if ratio not in (1,4):raise ValueError('不支持的扫描比例')
        if kind=='scan':load_profile(profile)
        if not BINARY.is_file():raise ValueError('请先构建 C 核心')
        with self.lock:
            if self.state['active']:raise RuntimeError('上一项操作尚未收尾')
            if kind=='scan' and not save_images:
                temporary=tempfile.TemporaryDirectory(prefix='f2400-preview-');output_root=Path(temporary.name)
                self.temporary_runs[str(output_root)]=temporary
            output_root.mkdir(parents=True,exist_ok=True)
            name=f'{kind}-{datetime.datetime.now():%Y%m%d-%H%M%S}-{uuid.uuid4().hex[:6]}'
            directory=output_root/name;logs=output_root/'.frontend-logs';logs.mkdir(exist_ok=True)
            self.cancel_event.clear()
            self.state=dict(active=True,kind=kind,initialize_required=self.state.get('initialize_required',False),message='正在启动…',progress=0,error=None,directory=str(directory),returncode=None,log=str(logs/f'{name}.log'),save_images=save_images,film=film,raw_mode=raw_mode)
        thread=threading.Thread(target=self._work,args=(kind,directory,profile,ratio,film,save_images,raw_mode),daemon=True);thread.start()
        return directory

    def discard_temporary(self,directory):
        """Call only after loading preview into memory, or after job completion."""
        if self.snapshot()['active']:raise RuntimeError('设备操作尚未结束，不能清理临时数据')
        key=str(Path(directory).parent)
        temporary=self.temporary_runs.pop(key,None)
        if temporary:
            self.update(log_text=tail(self.snapshot().get('log','')))
            temporary.cleanup()

    def close(self):
        if self.snapshot()['active']:raise RuntimeError('请等待设备收尾')
        for temporary in self.temporary_runs.values():temporary.cleanup()
        self.temporary_runs.clear()

    def _run(self,command,log,cancellable):
        with log.open('a') as stream:
            proc=subprocess.Popen(command,cwd=self.repo,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
            with self.lock:self.process=proc
            sent=False
            while proc.poll() is None:
                if cancellable and self.cancel_event.is_set() and not sent:
                    proc.send_signal(signal.SIGINT);sent=True
                time.sleep(.1)
            with self.lock:self.process=None
            return proc.returncode

    def _work(self,kind,directory,profile,ratio,film,save_images,raw_mode):
        log=Path(self.snapshot()['log']);code=None;error=None
        try:
            command=[str(BINARY),kind]
            if kind=='scan':
                command.extend(['--output',str(directory),'--calibration',str(Path(profile).resolve()),'--ratio',str(ratio),'--film','raw-color' if film=='raw' else film])
                if not save_images:command.extend(['--preview-only','--editable-preview'])
            elif kind in ('calibration','initialize'):
                command.extend(['--output',str(directory),'--holder-removed'])
            code=self._run(command,log,kind in ('scan','calibration','initialize'))
            if kind=='scan':
                report=read_json(directory/'result.json')
                if code==0 and report.get('status')!='complete':raise RuntimeError('缺少完整扫描与校验结果')
            if kind in ('calibration','initialize') and code==0:
                load_profile(directory/'profile.json')
            if code!=0 and code!=130:
                error=scan_failure(directory) if kind=='scan' else f'操作失败（退出码 {code}），请查看日志。'
                diagnostic=setup_failure(tail(directory/'capture.log') if kind=='calibration' else tail(log))
                if diagnostic:error=diagnostic
            if code==130 and kind=='initialize':error='初始化已中止，设备就绪状态未确认，请查看日志。'
            if code==130 and kind=='scan' and read_json(directory/'result.json').get('status')!='cancelled':
                error='进程中断，收尾状态未确认，请查看日志。'
            if code==0:message={'scan':'扫描完成，图片已保存' if save_images else '扫描完成，仅预览，未保存图片','calibration':'校准完成，档案已保存','initialize':'初始化与校准完成，请放回片夹后扫描','status':'扫描器已就绪','release':'收尾命令完成，扫描器已就绪'}[kind]
            elif code==130 and not error:message='已取消，驱动收尾完成'
            else:message='操作失败'
            if kind=='scan' and code==0 and report.get('meter',{}).get('exposure_limited'):
                message+='（测光受限，已按硬件范围采集）'
        except Exception as exc:
            error=str(exc);message='操作失败'
            if code in (None,0):code=1
        device_ready=None
        if kind=='scan':
            device_ready=capture_ready(directory,'full') if (directory/'full.log').exists() else capture_ready(directory,'meter')
        elif kind in ('status','release'):device_ready=code==0
        elif kind in ('calibration','initialize'):device_ready='release_ready=1' in tail(directory/'capture.log')
        if error:message='扫描未完成' if kind=='scan' else '操作失败'
        logs=tail(log)
        if kind=='scan':logs+='\n'+tail(directory/'full.log')+'\n'+tail(directory/'meter.log')
        if kind in ('calibration','initialize'):logs+='\n'+tail(directory/'capture.log')
        if kind=='initialize':logs+='\n'+tail(directory/'initialize.log')
        required=self.snapshot().get('initialize_required',False)
        if setup_failure(logs) or kind=='initialize' and code!=0:required=True
        if device_ready is True:required=False
        self.update(initialize_required=required,active=False,message=message,error=error,device_ready=device_ready,returncode=code,progress=1 if code==0 else 0)
