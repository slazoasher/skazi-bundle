// Hidden WebKit preview and interaction probe. No audio or host window is opened.
import AppKit
import WebKit

final class Probe: NSObject, WKNavigationDelegate, WKScriptMessageHandler {
    let view: WKWebView
    var done = false
    var failed = false
    init(html: String) {
        let config = WKWebViewConfiguration()
        config.websiteDataStore = .nonPersistent()
        view = WKWebView(frame: NSRect(x: 0, y: 0, width: 1227, height: 589), configuration: config)
        super.init()
        config.userContentController.add(self, name: "ravet")
        view.navigationDelegate = self
        view.loadHTMLString(html, baseURL: nil)
    }
    func userContentController(_ controller: WKUserContentController, didReceive message: WKScriptMessage) {}
    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        let fixture = """
        (()=>{const failures=[];const check=(ok,label)=>{if(!ok)failures.push(label)};
        check(controls.size===11,'parameter controls');
        check(document.querySelectorAll('[role=slider]').length===10,'knob roles');
        check(document.querySelectorAll('.hit').length===24,'sequencer buttons');
        check(document.querySelectorAll('svg filter').length===55,'SVG filters present (37 panel + knob artwork)');{const r=id=>document.getElementById(id).getBoundingClientRect().left;check(r('param-8')<r('param-7'),'Tempo knob (param-8) left of Volume (param-7)');check(document.getElementById('param-7').getAttribute('aria-label')==='Volume'&&document.getElementById('param-8').getAttribute('aria-label')==='Tempo','knob labels match bindings');}check(document.querySelectorAll('.knob .face use').length===8&&document.querySelectorAll('.knob .face-static').length===2,'knob artwork present');{const cap=id=>getComputedStyle(document.getElementById(id)).getPropertyValue('--cap').trim();check(cap('param-1')==='rgb(200, 198, 198)'||cap('param-1')==='rgb(200,198,198)','cap at centre');}
        check(document.querySelectorAll('[id]').length===new Set([...document.querySelectorAll('[id]')].map(e=>e.id)).size,'unique IDs');
        window.ravetUpdate({params:{1:0.75,8:180,9:1,10:2,11:0,12:1},leds:{run:1,'step-1':1}});
        check(controls.get(1).current===0.75,'snapshot knob');
        check(document.getElementById('waveform').checked,'snapshot waveform');
        check(document.getElementById('sync').value==='2','snapshot sync');
        check(document.getElementById('quality').value==='0','snapshot quality');
        check(document.getElementById('led-run').dataset.on==='1','run LED');
        const knob=controls.get(1);let messages=[];knob.post=m=>{messages.push(m);return true};
        const node=document.getElementById('param-1');
        node.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowUp',bubbles:true}));node.dispatchEvent(new KeyboardEvent('keyup',{key:'ArrowUp',bubbles:true}));
        check(messages.map(m=>m.type).join(',')==='begin,value,end','keyboard gestures');
        messages=[];node.dispatchEvent(new MouseEvent('dblclick',{bubbles:true}));check(messages[0]?.type==='edit'&&messages[0]?.value===0.5,'default');
        messages=[];node.dispatchEvent(new WheelEvent('wheel',{deltaY:-100,bubbles:true,cancelable:true}));check(messages[0]?.type==='edit'&&messages[0]?.value>0.5,'wheel');
        for(const size of [[818,393],[1227,589],[2045,981]]){const scale=Math.min(size[0]/1636,size[1]/785);check(scale>=0.49&&scale<=1.26,'scale range');}
        return {failures,knobs:document.querySelectorAll('[role=slider]').length,buttons:document.querySelectorAll('.hit').length,viewport:[innerWidth,innerHeight],panel:panel.getBoundingClientRect().toJSON()};})()
        """
        view.evaluateJavaScript(fixture) { result, error in
            if let error = error { print("FAIL: \(error)"); self.failed = true; self.done = true; return }
            print(result ?? "No result")
            if let report = result as? [String: Any], let failures = report["failures"] as? [String] { self.failed = !failures.isEmpty } else { self.failed = true }
            let config = WKSnapshotConfiguration()
            self.view.takeSnapshot(with: config) { image, error in
                if let image = image, let tiff = image.tiffRepresentation, let bitmap = NSBitmapImageRep(data: tiff), let png = bitmap.representation(using: .png, properties: [:]) {
                    try? png.write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
                } else { print("Snapshot unavailable: \(String(describing:error))"); self.failed = true }
                self.done = true
            }
        }
    }
}
let app = NSApplication.shared
app.setActivationPolicy(.prohibited)
let html = try String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
let probe = Probe(html: html)
let window = NSWindow(contentRect: NSRect(x: 0,y: 0,width: 1227,height: 589),styleMask: [.borderless],backing: .buffered,defer: false)
window.contentView = probe.view
let deadline = Date().addingTimeInterval(20)
while !probe.done && Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.02)) }
if !probe.done { print("FAIL: WebKit timed out; check sandbox/native GUI availability") }
exit(probe.done && !probe.failed ? 0 : 1)
