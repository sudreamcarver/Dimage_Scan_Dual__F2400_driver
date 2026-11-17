#!/usr/bin/env python3
"""F-2400 independent Linux desktop frontend (Tk + Pillow)."""
import argparse,fcntl,json,subprocess,sys
from pathlib import Path
import tkinter as tk
from tkinter import ttk,filedialog,messagebox
from PIL import Image,ImageTk,ImageOps
from controller import Operations,ROOT,read_json,scan_progress,tail,FILM_TYPES
from native import load_profile, PreviewDocument


class Desktop:
    def __init__(self,root,operations=None):
        self.root=root;self.ops=operations or Operations();self.closing=False;self.was_active=False
        self.document=None;self.preview=None;self.photo=None;self.last_directory=None;self.preview_path=None;self.preview_name=''
        self.advanced_open=False
        root.title('Minolta F-2400 · 胶片扫描');root.geometry('1180x780');root.minsize(740,580)
        style=ttk.Style();style.theme_use('clam')
        style.configure('.',font=('Sans',10))
        style.configure('TButton',padding=(10,7))
        style.configure('Title.TLabel',font=('Sans',18,'bold'))
        style.configure('Section.TLabel',font=('Sans',11,'bold'))
        style.configure('Sub.TLabel',foreground='#626c78')
        style.configure('Primary.TButton',font=('Sans',11,'bold'),padding=(12,12),background='#465867',foreground='#ffffff')
        style.map('Primary.TButton',background=[('disabled','#b8b9b6'),('active','#364957')],foreground=[('disabled','#eeeeeb')])
        outer=ttk.Frame(root,padding=18);outer.pack(fill='both',expand=True)
        header=ttk.Frame(outer);header.pack(fill='x',pady=(0,16))
        ttk.Label(header,text='F-2400',style='Title.TLabel').pack(side='left')
        ttk.Label(header,text='胶片扫描',style='Sub.TLabel').pack(side='left',padx=(12,0))
        self.buttons=[]
        self.button(header,'退出片夹',lambda:self.start('release')).pack(side='right')
        self.button(header,'检查设备',lambda:self.start('status')).pack(side='right',padx=(0,8))
        self.device_label=ttk.Label(header,text='设备未检查',style='Sub.TLabel');self.device_label.pack(side='right',padx=14)
        content=ttk.Frame(outer);content.pack(fill='both',expand=True)
        self.content=content;self.layout_mode=None
        self.control_host=ttk.Frame(content,width=290)
        self.control_host.pack(side='left',fill='y',padx=(0,18))
        # Keep the primary action visible while secondary settings scroll.
        action=ttk.Frame(self.control_host,padding=(0,12,0,0));action.pack(side='bottom',fill='x')
        self.scan_button=self.button(action,'开始扫描',lambda:self.start('scan'));self.scan_button.configure(style='Primary.TButton');self.scan_button.pack(fill='x')
        scroll=ttk.Frame(self.control_host);scroll.pack(fill='both',expand=True)
        control_canvas=tk.Canvas(scroll,width=276,highlightthickness=0,bg=style.lookup('TFrame','background'))
        scrollbar=ttk.Scrollbar(scroll,orient='vertical',command=control_canvas.yview)
        control_canvas.pack(side='left',fill='both',expand=True)
        control_canvas.configure(yscrollcommand=lambda first,last:self.scroll_changed(scrollbar,first,last))
        left=ttk.Frame(control_canvas,padding=(0,2,12,0))
        window=control_canvas.create_window((0,0),window=left,anchor='nw')
        left.bind('<Configure>',lambda _:control_canvas.configure(scrollregion=control_canvas.bbox('all')))
        control_canvas.bind('<Configure>',lambda e:control_canvas.itemconfigure(window,width=e.width))
        control_canvas.bind('<Button-4>',lambda _:control_canvas.yview_scroll(-2,'units'))
        control_canvas.bind('<Button-5>',lambda _:control_canvas.yview_scroll(2,'units'))
        control_canvas.bind('<MouseWheel>',lambda e:control_canvas.yview_scroll(-int(e.delta/120),'units'))
        self.section(left,'扫描设置')
        ttk.Label(left,text='胶片类型').pack(anchor='w',pady=(12,5))
        self.film=tk.StringVar(value=next(iter(FILM_TYPES)))
        self.film_box=ttk.Combobox(left,textvariable=self.film,state='readonly',values=list(FILM_TYPES));self.film_box.pack(fill='x')
        self.film_box.bind('<<ComboboxSelected>>',lambda _:self.film_changed())
        self.film_warning=ttk.Label(left,text='此类型暂未支持',style='Sub.TLabel')
        ttk.Label(left,text='分辨率').pack(anchor='w',pady=(16,5))
        self.resolution=tk.StringVar(value='全分辨率 · 2336 × 3504')
        self.resolution_box=ttk.Combobox(left,textvariable=self.resolution,state='readonly',values=('全分辨率 · 2336 × 3504','快速扫描 · 584 × 876'));self.resolution_box.pack(fill='x')
        ttk.Separator(left).pack(fill='x',pady=20)
        self.save_images=tk.BooleanVar(value=False)
        self.save_check=ttk.Checkbutton(left,text='自动保存',variable=self.save_images,command=self.save_changed);self.save_check.pack(anchor='w')
        self.output=tk.StringVar(value=str(ROOT/'reports'/'desktop'))
        self.output_row=ttk.Frame(left,padding=(0,8,0,0))
        self.output_entry=ttk.Entry(self.output_row,textvariable=self.output);self.output_entry.pack(side='left',fill='x',expand=True)
        self.output_button=self.button(self.output_row,'目录…',self.choose_output);self.output_button.pack(side='right',padx=(6,0))
        # Secondary controls remain one click away, and open for initialization errors.
        self.advanced_toggle=ttk.Button(left,text='▸  设备与校准',command=self.toggle_advanced);self.advanced_toggle.pack(fill='x',pady=(22,0))
        self.advanced=ttk.Frame(left,padding=(0,14,0,4))
        ttk.Label(self.advanced,text='校准档案').pack(anchor='w',pady=(0,5))
        self.profile=tk.StringVar()
        self.profile_box=ttk.Combobox(self.advanced,textvariable=self.profile,state='readonly');self.profile_box.pack(fill='x')
        self.profile_box.bind('<<ComboboxSelected>>',lambda _:self.profile_info())
        details=ttk.Frame(self.advanced);details.pack(fill='x',pady=(6,12))
        self.profile_label=ttk.Label(details,text='',style='Sub.TLabel');self.profile_label.pack(side='left')
        self.button(details,'载入…',self.choose_profile).pack(side='right')
        ttk.Separator(self.advanced).pack(fill='x',pady=(0,12))
        self.holder_removed=tk.BooleanVar(value=False)
        self.holder_check=ttk.Checkbutton(self.advanced,text='片夹已取下，入口无遮挡',variable=self.holder_removed);self.holder_check.pack(anchor='w')
        self.button(self.advanced,'冷启动初始化',lambda:self.start('initialize')).pack(fill='x',pady=(10,6))
        self.button(self.advanced,'刷新校准',lambda:self.start('calibration')).pack(fill='x')
        right=ttk.Frame(content);self.preview_host=right;right.pack(side='left',fill='both',expand=True)
        toolbar=ttk.Frame(right);toolbar.pack(fill='x',pady=(0,10))
        ttk.Label(toolbar,text='预览',style='Section.TLabel').pack(side='left',padx=(0,12))
        ttk.Button(toolbar,text='打开…',command=self.choose_result).pack(side='left')
        ttk.Button(toolbar,text='目录',command=self.open_result).pack(side='left',padx=(6,0))
        self.save_preview_button=ttk.Button(toolbar,text='另存为…',command=self.save_preview,state='disabled');self.save_preview_button.pack(side='right')
        self.rotate_right=ttk.Button(toolbar,text='右转90°',command=lambda:self.rotate_preview(True),state='disabled');self.rotate_right.pack(side='right',padx=(6,12))
        self.rotate_left=ttk.Button(toolbar,text='左转90°',command=lambda:self.rotate_preview(False),state='disabled');self.rotate_left.pack(side='right')
        self.canvas=tk.Canvas(right,bg='#14202d',highlightthickness=0);self.canvas.pack(fill='both',expand=True);self.canvas.bind('<Configure>',lambda _:self.redraw())
        self.image_label=ttk.Label(right,text='尚无预览',style='Sub.TLabel');self.image_label.pack(anchor='w',pady=(8,0))
        ttk.Separator(outer).pack(fill='x',pady=(14,10))
        bottom=ttk.Frame(outer);bottom.pack(fill='x')
        self.status=tk.StringVar(value='准备就绪')
        self.status_label=ttk.Label(bottom,textvariable=self.status,wraplength=780);self.status_label.pack(side='left',fill='x',expand=True)
        self.cancel_button=ttk.Button(bottom,text='取消',command=self.ops.cancel,state='disabled');self.cancel_button.pack(side='right')
        ttk.Button(bottom,text='日志',command=self.show_log).pack(side='right',padx=(10,8))
        self.progress=ttk.Progressbar(outer,maximum=100);self.progress.pack(fill='x',pady=(8,0))
        self.notebook=ttk.Notebook(content)
        self.settings_tab=ttk.Frame(self.notebook);self.preview_tab=ttk.Frame(self.notebook)
        self.notebook.add(self.settings_tab,text='扫描设置');self.notebook.add(self.preview_tab,text='预览')
        root.bind('<Configure>',self.responsive)
        self.profiles={};self.refresh_profiles();self.save_changed()
        latest=max([*(ROOT/'reports').glob('**/image-factory.png'),*(ROOT/'reports').glob('**/image-raw.png')],key=lambda p:p.stat().st_mtime,default=None)
        if latest:
            self.load_preview(latest)
            report=read_json(latest.parent/'result.json')
            film='raw-bw' if report.get('raw_channels')==1 or report.get('film')=='35mm BW RAW' else 'raw-color' if latest.stem=='image-raw' else {'35mm color negative':'color-negative','35mm bw negative':'bw-negative'}.get(report.get('film'),'color-positive')
            self.film.set(next(name for name,value in FILM_TYPES.items() if value==film));self.film_changed()
        root.protocol('WM_DELETE_WINDOW',self.close);root.after(200,self.tick)

    def section(self,parent,text):
        ttk.Label(parent,text=text,style='Section.TLabel').pack(anchor='w')

    def scroll_changed(self,scrollbar,first,last):
        scrollbar.set(first,last)
        if float(first)>0 or float(last)<1:scrollbar.pack(side='right',fill='y')
        else:scrollbar.pack_forget()

    def toggle_advanced(self,opened=None):
        self.advanced_open=not self.advanced_open if opened is None else opened
        self.advanced_toggle.configure(text=('▾' if self.advanced_open else '▸')+'  设备与校准')
        if self.advanced_open:self.advanced.pack(fill='x')
        else:self.advanced.pack_forget()

    def responsive(self,event):
        if event.widget is not self.root:return
        self.status_label.config(wraplength=max(280,event.width-220))
        mode='narrow' if event.width<980 else 'wide'
        if mode==self.layout_mode:return
        self.layout_mode=mode;self.control_host.pack_forget();self.preview_host.pack_forget();self.notebook.pack_forget()
        if mode=='narrow':
            self.notebook.pack(fill='both',expand=True)
            self.control_host.pack(in_=self.settings_tab,fill='both',expand=True)
            self.preview_host.pack(in_=self.preview_tab,fill='both',expand=True)
            self.control_host.lift();self.preview_host.lift()
        else:
            self.control_host.pack(in_=self.content,side='left',fill='y',padx=(0,18))
            self.preview_host.pack(in_=self.content,side='left',fill='both',expand=True)

    def button(self,parent,text,command):
        b=ttk.Button(parent,text=text,command=command);self.buttons.append(b);return b

    def refresh_profiles(self,selected=None):
        previous=self.profiles.get(self.profile.get())
        paths=sorted((ROOT/'reports').glob('**/profile.json'),key=lambda p:p.stat().st_mtime,reverse=True)
        if selected:paths.insert(0,Path(selected))
        self.profiles={};seen=set()
        for path in paths:
            if path in seen:continue
            try:_,_,meta=load_profile(path)
            except (OSError,ValueError,KeyError,TypeError):continue
            seen.add(path)
            stamp=meta.get('created_utc','')[:16].replace('T',' ')
            label=stamp or path.parent.name
            key=label;index=2
            while key in self.profiles:key=f'{label} · {index}';index+=1
            self.profiles[key]=path
        self.profile_box['values']=list(self.profiles)
        chosen=Path(selected) if selected else previous
        self.profile.set(next((k for k,v in self.profiles.items() if v==chosen),next(iter(self.profiles),'')))
        self.profile_info()

    def profile_info(self):
        path=self.profiles.get(self.profile.get())
        if path:
            try:
                _,base,meta=load_profile(path);self.profile_label.config(text='校准有效')
            except Exception as exc:self.profile_label.config(text=str(exc))
        else:self.profile_label.config(text='未选择档案')

    def choose_profile(self):
        name=filedialog.askopenfilename(title='选择校准档案',initialdir=ROOT/'reports',filetypes=[('校准档案','*.json')])
        if name:
            try:load_profile(name);self.refresh_profiles(Path(name))
            except Exception as exc:messagebox.showerror('校准档案不可用',str(exc))

    def film_changed(self):
        supported=FILM_TYPES.get(self.film.get()) in ('color-positive','bw-negative','raw','raw-color','raw-bw','color-negative')
        if supported:self.film_warning.pack_forget()
        else:self.film_warning.pack(after=self.film_box,anchor='w',pady=(6,0))
        state=self.ops.snapshot()
        self.scan_button.config(state='normal' if supported and not state['active'] and not state.get('initialize_required') else 'disabled')

    def save_changed(self):
        enabled=self.save_images.get() and not self.ops.snapshot()['active']
        self.output_entry.config(state='normal' if enabled else 'disabled');self.output_button.config(state='normal' if enabled else 'disabled')
        if self.save_images.get():self.output_row.pack(after=self.save_check,fill='x')
        else:self.output_row.pack_forget()

    def choose_output(self):
        name=filedialog.askdirectory(initialdir=self.output.get())
        if name:self.output.set(name)

    def start(self,kind):
        if kind in ('calibration','initialize') and not self.holder_removed.get():
            self.toggle_advanced(True);self.status.set('请取下片夹并勾选确认。');return
        if kind=='scan':
            self.holder_removed.set(False)
            if self.preview:self.image_label.config(text=f'上次成片（本次扫描尚未完成）：{self.preview_name}')
        try:
            directory=self.ops.start(kind,self.output.get(),self.profiles.get(self.profile.get()),1 if self.resolution.get().startswith('全') else 4,film=FILM_TYPES.get(self.film.get()),save_images=self.save_images.get())
            if kind in ('calibration','initialize') or (kind=='scan' and self.save_images.get()):self.last_directory=directory
            self.device_label.config(text='操作中…');self.set_busy(True,kind);self.was_active=True
        except Exception as exc:messagebox.showerror('无法开始',str(exc))

    def set_busy(self,busy,kind=None):
        for button in self.buttons:button.config(state='disabled' if busy else 'normal')
        for entry in (self.resolution_box,self.profile_box,self.film_box):entry.config(state='disabled' if busy else 'readonly')
        self.output_entry.config(state='disabled' if busy else 'normal');self.holder_check.config(state='disabled' if busy else 'normal')
        self.save_check.config(state='disabled' if busy else 'normal')
        if not busy:self.save_changed();self.film_changed()
        self.cancel_button.config(state='normal' if busy and kind in ('scan','calibration','initialize') else 'disabled')

    def tick(self):
        s=self.ops.snapshot();text=s['message'];fraction=s['progress']
        if s['active'] and not self.ops.cancel_event.is_set():
            if s['kind']=='scan':text,fraction,_=scan_progress(s['directory'],1 if self.resolution.get().startswith('全') else 4)
            elif s['kind'] in ('calibration','initialize'):
                import re
                matches=re.findall(r'CALIBRATION field=(\S+) sample=(\d+)',tail(Path(s['directory'])/'capture.log'))
                if s['kind']=='initialize' and not matches:
                    import re
                    stages=re.findall(r'INITIALIZE stage=(\S+)',tail(Path(s['directory'])/'initialize.log'))
                    names={'light':'光源检查','offsets':'偏置搜索','dark-wide':'单通道暗场','dark-color':'彩色暗场','peak':'定位最亮区','stability':'等待光源稳定','exposure':'计算曝光','dark-field':'采集暗场','white-first':'第一遍白场','white-second':'第二遍白场'}
                    if stages:text='冷启动初始化：'+names.get(stages[-1],stages[-1]);fraction=.1
                if matches:
                    field,n=matches[-1];index={'dark-sums':0,'white-sums':1,'correction-sums':2}.get(field,0)
                    text=f'{("暗场","第一遍白场","第二遍白场")[index]}：{n} / 64 次';fraction=(index*64+int(n))/192
        self.status.set(text+(f'  {s["error"]}' if s['error'] else ''));self.progress['value']=fraction*100
        if self.was_active and not s['active']:
            self.set_busy(False);self.was_active=False
            self.device_label.config(text='● 已就绪' if s.get('device_ready') is True else '状态待确认')
            if s['error'] and s['kind']=='scan' and self.preview:self.image_label.config(text=f'上次成片（本次未完成）：{self.preview_name}')
            if s['returncode']==0:
                if s['kind']=='scan':
                    loaded=self.load_preview(Path(s['directory'])/('image-raw.png' if s.get('raw_mode') else 'image-factory.png'),saved=s.get('save_images',True))
                    if loaded and not s.get('save_images',True):self.ops.discard_temporary(s['directory'])
                if s['kind'] in ('calibration','initialize'):self.refresh_profiles(Path(s['directory'])/'profile.json');self.holder_removed.set(False)
                if s['kind'] in ('status','release'):self.device_label.config(text='● 已就绪')
            elif s.get('initialize_required'):
                self.device_label.config(text='需要初始化');self.toggle_advanced(True)
                if self.layout_mode=='narrow':self.notebook.select(self.settings_tab)
            elif s['kind'] in ('status','release'):self.device_label.config(text='状态待确认')
        if self.closing and not s['active']:self.ops.close();self.root.destroy();return
        self.root.after(250,self.tick)

    def load_preview(self,path,saved=True):
        try:
            self.document=PreviewDocument.load(path);self.preview=self.document.image
            for button in (self.rotate_left,self.rotate_right,self.save_preview_button):button.config(state='normal')
            self.preview_path=Path(path) if saved else None;self.last_directory=Path(path).parent if saved else None
            self.preview_name=path.name if saved else '未保存的扫描'
            self.image_label.config(text=f'{self.preview_name}  ·  {self.preview.width} × {self.preview.height}');self.redraw();return True
        except Exception as exc:self.status.set(f'预览无法读取：{exc}');return False

    def rotate_preview(self,clockwise=True):
        if self.document is None:return
        self.document.rotate(clockwise);self.preview=self.document.image;self.redraw()
        self.image_label.config(text=f'{self.preview_name} · 旋转 {self.document.turns*90}° · {self.preview.width} × {self.preview.height}')

    def save_preview(self):
        if self.document is None:return
        name=filedialog.asksaveasfilename(title='保存当前图片（含当前旋转方向）',initialdir=self.output.get(),initialfile='scan.tiff' if self.document.high_depth is not None else 'scan.png',defaultextension='.tiff' if self.document.high_depth is not None else '.png',filetypes=[('TIFF 图片','*.tiff'),('PNG 图片','*.png')])
        if not name:return
        try:
            self.document.save(name);self.preview_path=Path(name);self.last_directory=Path(name).parent;self.preview_name=Path(name).name
            self.document.turns=0;self.image_label.config(text=f'已另存：{Path(name).name} · {self.preview.width} × {self.preview.height}')
            messagebox.showinfo('已保存',f'已保存当前方向和原始分辨率：\n{name}')
        except Exception as exc:messagebox.showerror('保存失败',str(exc))

    def redraw(self):
        self.canvas.delete('all');w=self.canvas.winfo_width();h=self.canvas.winfo_height()
        if self.preview and w>10 and h>10:
            im=ImageOps.contain(self.preview,(max(1,w-24),max(1,h-24)));self.photo=ImageTk.PhotoImage(im)
            self.canvas.create_image(w//2,h//2,image=self.photo)
        else:self.canvas.create_text(w//2,h//2,text='扫描或打开图片',fill='#aabbd0',font=('Sans',15))

    def choose_result(self):
        name=filedialog.askopenfilename(title='打开扫描成片',initialdir=self.output.get(),filetypes=[('扫描成片','*.png *.tiff *.tif')])
        if name:self.load_preview(Path(name))

    def open_result(self):
        if self.last_directory and self.last_directory.exists():subprocess.Popen(['xdg-open',str(self.last_directory)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

    def show_log(self):
        s=self.ops.snapshot();text=tail(s.get('log','')) or s.get('log_text','')
        if s.get('directory'):
            for name in ('meter.stderr','full.stderr','capture.stderr','initialize.log','meter.log','full.log','capture.log'):
                value=tail(Path(s['directory'])/name)
                if value:text+=f'\n\n{name}\n{value[-18000:]}'
        top=tk.Toplevel(self.root);top.title('运行日志');top.geometry('920x600')
        area=tk.Text(top,wrap='none');area.pack(fill='both',expand=True);area.insert('1.0',text or '尚无运行日志');area.config(state='disabled')

    def close(self):
        if self.ops.snapshot()['active']:
            self.closing=True;self.ops.cancel();self.status.set('等待当前操作收尾后关闭…')
        else:self.ops.close();self.root.destroy()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--smoke',action='store_true',help='create widgets and preview, then exit; no scanner commands');args=p.parse_args()
    import os
    runtime=Path(os.environ.get('XDG_RUNTIME_DIR','/tmp'))/f'f2400-{os.getuid()}'
    runtime.mkdir(mode=0o700,exist_ok=True)
    with (runtime/'frontend.lock').open('w') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise SystemExit('桌面前端已在运行。')
        root=tk.Tk();app=Desktop(root)
        if args.smoke:root.update();root.after(500,root.destroy)
        else:root.after(400,lambda:app.start('status'))
        root.mainloop()

if __name__=='__main__':main()
