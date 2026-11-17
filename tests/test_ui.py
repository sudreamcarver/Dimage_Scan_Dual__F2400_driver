"""GUI interaction checks; all image/calibration operations invoke the C API."""
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import tkinter as tk
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'ui'))
from native import lib, load_profile, PreviewDocument
import controller
spec = importlib.util.spec_from_file_location('desktop', ROOT / 'ui' / 'desktop.py')
desktop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)

class UiTests(unittest.TestCase):
    def profile(self, folder):
        shutil.copyfile(ROOT / 'tests/fixtures/calibration.json', folder / 'profile.json')
        shutil.copyfile(ROOT / 'tests/fixtures/calibration-shading.bin', folder / 'shading.bin')
        return folder / 'profile.json'

    def test_profile_validation_is_native(self):
        with tempfile.TemporaryDirectory() as temp:
            path = self.profile(Path(temp))
            self.assertEqual(load_profile(path)[1], [36, 20, 25])
            (path.parent / 'shading.bin').write_bytes(bytes(7008))
            with self.assertRaises(ValueError):load_profile(path)
            path.write_text('{"device":null,"schema":1}')
            with self.assertRaises(ValueError):load_profile(path)

    def test_cli_job_dispatch_and_cancel(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            profile = self.profile(folder)
            fake = folder / 'fake'
            fake.write_text('''#!/usr/bin/env python3
import sys,time,signal,json
from pathlib import Path
args=sys.argv[1:];p=Path(args[args.index('--output')+1]);p.mkdir()
(p/'args.json').write_text(json.dumps(args))
signal.signal(signal.SIGINT,lambda *a:sys.exit(130))
(p/'started').touch()
time.sleep(30)
''')
            fake.chmod(0o755)
            with patch.object(controller, 'BINARY', fake):
                op = controller.Operations(folder)
                run = op.start('scan', folder / 'out', profile, ratio=4, film='raw-bw', save_images=False)
                deadline = time.monotonic() + 5
                while not (run / 'started').exists():
                    self.assertLess(time.monotonic(), deadline);time.sleep(.02)
                args = json.loads((run / 'args.json').read_text())
                self.assertEqual(args[0], 'scan')
                self.assertIn('--editable-preview', args)
                self.assertIn('raw-bw', args)
                op.cancel()
                while op.snapshot()['active']:
                    self.assertLess(time.monotonic(), deadline);time.sleep(.02)
                self.assertIsNotNone(op.snapshot()['error'])
                op.discard_temporary(run)
                self.assertFalse(run.exists())
                op.close()

    def test_layout_preview_and_maintenance_confirmation(self):
        try:root = tk.Tk()
        except tk.TclError as exc:self.skipTest(str(exc))
        try:
            app = desktop.Desktop(root)
            root.geometry('1180x780');root.update()
            self.assertFalse(app.advanced.winfo_ismapped())
            self.assertFalse(app.output_row.winfo_ismapped())
            with patch.object(app.ops, 'start', return_value=ROOT) as start:
                app.start('initialize');start.assert_not_called()
                self.assertTrue(app.advanced_open)
                app.holder_removed.set(True);app.start('initialize')
                self.assertEqual(start.call_args.args[0], 'initialize')
            app.was_active = False
            app.set_busy(False)
            root.update()
            if app.layout_mode == 'narrow':
                app.notebook.select(app.preview_tab);root.update()
            app.ops.update(initialize_required=True);app.film_changed()
            self.assertEqual(str(app.scan_button['state']), 'disabled')
            from PIL import Image
            with tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                source = folder / 'source.png'
                Image.new('RGB', (80, 120), '#416279').save(source)
                app.load_preview(source, saved=False)
                app.document.save(folder / 'source.tiff')
                app.load_preview(source, saved=False)
                source.unlink();(folder / 'source.tiff').unlink()
                app.rotate_preview(True);root.update()
                self.assertEqual(app.preview.size, (120, 80))
                bounds = app.canvas.bbox('all')
                self.assertGreaterEqual(bounds[0], 0)
                self.assertLessEqual(bounds[2], app.canvas.winfo_width())
                with patch.object(desktop.filedialog, 'asksaveasfilename', return_value=str(folder / 'manual.tiff')), patch.object(desktop.messagebox, 'showinfo'):
                    app.save_preview()
                self.assertFalse(app.save_images.get())
                im = PreviewDocument.load(folder / 'manual.tiff')
                self.assertEqual(im.image.size, (120, 80));im.close()
            root.geometry('800x650');root.update()
            self.assertEqual(app.layout_mode, 'narrow')
            app.notebook.select(app.preview_tab);root.update()
            for button in (app.rotate_left, app.rotate_right, app.save_preview_button):
                self.assertLessEqual(button.winfo_rootx() + button.winfo_width(), root.winfo_rootx() + root.winfo_width())
            app.ops.close()
        finally:root.destroy()

if __name__ == '__main__':unittest.main()
