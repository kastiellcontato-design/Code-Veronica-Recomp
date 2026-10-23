"""Loopback-only updater smoke test with a fake release and hostile ZIP fixtures."""
from pathlib import Path
import argparse,hashlib,http.server,json,os,subprocess,tempfile,threading,zipfile,shutil,time

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--test-exe',type=Path,required=True,help='code_veronica_updater_tests.exe')
    args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='cv-update-http-') as tmp:
        root=Path(tmp);bad=root/'invalid';bad.mkdir()
        for name,entry,attr in [('traversal','../outside.txt',0),('drive','C:/outside.txt',0),
                                ('device','CON.exe',0),('alias','saves./oops.exe',0),
                                ('ads','file.dll:stream',0),('link','escape',0o120777<<16)]:
            with zipfile.ZipFile(bad/f'{name}.zip','w') as z:
                z.writestr('CodeVeronicaPort.exe',b'wrong exe')
                info=zipfile.ZipInfo(entry);info.create_system=3;info.external_attr=attr
                z.writestr(info,b'../outside' if name=='link' else b'bad')
        (bad/'truncated.zip').write_bytes(b'PK\x03\x04broken')
        with zipfile.ZipFile(root/'CodeVeronica-PC-v9.9.zip','w',zipfile.ZIP_DEFLATED) as z:
            z.writestr('CodeVeronicaPort.exe',b'HTTP fake updated executable')
            z.writestr('cv_launcher.ini',b'overwrite settings')
            z.writestr('gamedata/save.bin',b'overwrite save')
        package=(root/'CodeVeronica-PC-v9.9.zip').read_bytes()
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path=='/latest.json':
                    data=json.dumps({'tag_name':'v9.9','name':'Local test','body':'Offline smoke test',
                      'draft':False,'prerelease':False,'assets':[{'name':'CodeVeronica-PC-v9.9.zip',
                      'browser_download_url':f'http://127.0.0.1:{self.server.server_port}/package.zip',
                      'size':len(package),'digest':'sha256:'+hashlib.sha256(package).hexdigest()}]}).encode()
                elif self.path=='/package.zip':data=package
                else:self.send_error(404);return
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
            def log_message(self,*args):pass
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
        worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
        env=os.environ.copy();env['CV_UPDATE_URL']=f'http://127.0.0.1:{server.server_port}/latest.json'
        env['CV_UPDATER_FIXTURES']=str(bad)
        try:subprocess.run([str(args.test_exe.resolve())],env=env,check=True)
        finally:server.shutdown();server.server_close();worker.join()
        live=root/'running-port';live.mkdir()
        current=live/'CodeVeronicaPort.exe';shutil.copy2(args.test_exe,current)
        running_zip=root/'running-update.zip'
        with zipfile.ZipFile(running_zip,'w',zipfile.ZIP_DEFLATED) as z:z.write(args.test_exe,'CodeVeronicaPort.exe')
        subprocess.run([str(current),'--apply-running',str(running_zip)],check=True,timeout=45)
        marker=live/'new-started.txt'
        deadline=time.monotonic()+30
        while not marker.exists() and time.monotonic()<deadline:time.sleep(.1)
        assert marker.read_text()=='updated launcher started'
        assert not (live/'CodeVeronicaPort.exe.cvupd-old').exists()
        assert not (live/'.cvupd-cleanup').exists()
        print('Running executable: replacement + restart + old file cleanup passed.')
        print('Local HTTP release: fetch + SHA-256 + install + settings/save preservation passed.')
        print('Hostile ZIP fixtures: traversal, drive paths, devices, aliases, ADS, symlink, truncation rejected.')
if __name__=='__main__':main()
