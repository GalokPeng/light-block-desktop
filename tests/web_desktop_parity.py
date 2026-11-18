"""End-to-end checks against the static site. Run with a Playwright-enabled Python and Chrome."""
import argparse
import base64
import hashlib
import io
import json
from pathlib import Path

from PIL import Image
from playwright.sync_api import expect, sync_playwright

ROOT = Path(__file__).resolve().parents[1]
MEDIA = ROOT / "site/public/desktop/media"


def exercise(page, base):
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto(base + "/?lang=zh", wait_until="domcontentloaded")
    frame = page.frame_locator(".lab-monitor iframe")
    at = frame.locator
    act = lambda name: at(f'[data-act="{name}"]').first.click()
    cmd = lambda name: at(f'[data-cmd="{name}"]').first.click()
    expect(at("#rail button[aria-pressed]")).to_have_count(6)
    expect(at("#desk")).to_have_class("native-pointer")
    expect(page.locator(".lab-caption span")).to_have_text("预览体验有限，安装体验原汁原味功能")
    before = at("#wall").get_attribute("src")
    act("wall:1")
    assert at("#wall").get_attribute("src") == before
    act("apply")
    assert "milky-way" in at("#wall").get_attribute("src")
    act("wall:5")
    act("preview")
    expect(at(".spot video")).to_be_visible()
    assert at(".spot video").evaluate("async v=>{await v.play();return !v.paused}")
    act("show:cursor")
    at('[data-group="pointer"] summary').click()
    act("scheme:pixel")
    at('[data-key="particles"]').check()
    at('[data-key="effect"]').select_option("fall")
    at('[data-key="particleColor"]').fill("#ff6600")
    at('[data-key="size"]').focus()
    at('[data-key="size"]').press("ArrowRight")
    assert at("#desk").evaluate("e=>getComputedStyle(e).getPropertyValue('--particle-color')") == "#ff6600"
    at('#win-actions [data-act="hide"]').first.click()
    at("[data-shortcut]").dblclick()
    expect(at("#app")).to_be_visible()
    act("show:about")
    expect(at(".about-card")).to_contain_text("v2.0.6")
    act("theme")
    expect(at("#app")).to_have_attribute("data-theme", "light")
    act("language")
    expect(at("html")).to_have_attribute("lang", "en")
    expect(at("#app")).to_have_attribute("data-theme", "light")
    act("show:wall")
    assert "milky-way" in at("#wall").get_attribute("src")
    act("library:online")
    for kind in ("image", "gif", "video"):
        at(f'.catalog [data-type="{kind}"]').click()
        expect(at(".catalog-grid button").first).to_be_visible()
        at("[data-preview]").click()
        expect(at(".online-player :is(img,video)")).to_be_visible()
        at("[data-preview]").click()
        expect(at(".catalog-grid")).to_be_visible()
    act("show:capture")
    act("capture")
    expect(at("[data-cmd=smart]")).to_be_visible()
    canvas = at("#capture-layer canvas")
    bounds = canvas.bounding_box()
    x, y = round(bounds["x"]), round(bounds["y"])
    def stroke(x1,y1,x2,y2):
        page.mouse.move(x+x1,y+y1)
        page.mouse.down()
        page.mouse.move(x+x2,y+y2,steps=5)
        page.mouse.up()
    stroke(95,80,355,220)
    expect(at("[data-cmd=pin]")).to_be_visible()
    cmd("rect")
    at('[data-opt=fill]').check()
    stroke(120,110,200,180)
    cmd("mosaic")
    at("[data-opt=radius]").select_option("12")
    cmd("mosaic_brush")
    cmd("erase")
    at("[data-opt=eraser]").select_option("32")
    stroke(140,145,165,145)
    cmd("undo")
    cmd("redo")
    cmd("pin")
    pinned = at(".pinned-view img")
    expect(pinned).to_be_visible()
    buff = base64.b64decode(pinned.get_attribute("src").split(",")[1])
    img = Image.open(io.BytesIO(buff)).convert("RGB")
    assert img.size == (260,140), img.size
    assert img.getpixel((29,42)) == (255,104,104), img.getpixel((29,42))
    assert img.getpixel((56,65)) != (255,104,104)
    pinned.hover()
    page.mouse.wheel(0,110)
    expect(at(".native-pin")).to_have_css("opacity","0.95")
    pinned.click(button="right")
    at('[data-pin=edge]').click()
    expect(at(".native-pin")).to_have_class("result native-pin borderless")
    pinned.click(button="right")
    at('[data-pin=edit]').click()
    cmd("done")
    expect(pinned).to_be_visible()
    pinned.click(button="right")
    at('[data-pin=close]').click()
    at("[data-shortcut]").dblclick()
    act("long")
    cmd("full")
    cmd("longshot")
    at(".long-session [data-act=next]").click()
    expect(at(".long-session small")).to_contain_text("Same frame", timeout=10000)
    at(".long-session [data-act=finish]").click()
    expect(pinned).to_be_visible()
    pinned.click(button="right")
    at('[data-pin=close]').click()
    at("[data-shortcut]").dblclick()
    act("record")
    expect(at('[data-cmd=smart]')).to_be_visible()
    stroke(85,70,375,210)
    cmd("record")
    expect(at("#rec-state")).to_be_visible()
    page.wait_for_timeout(1100)
    at("#rec-state button").click()
    expect(at(".result video")).to_be_visible()
    at(".result video").evaluate("v=>v.play()")
    assert at(".result video").evaluate("v=>v.videoWidth") == 290
    at('[data-trim="0"]').press("ArrowRight")
    at("[data-format]").select_option("image/gif")
    with page.expect_download(timeout=20000) as downloaded:
        at(".studio-controls [data-act=export]").click()
    exported = Image.open(downloaded.value.path())
    assert exported.format == "GIF" and exported.size == (290,140)
    assert not errors, errors


def fixtures(page, base):
    """All three preview paths decode local material independent of third-party uptime."""
    jpg = (ROOT / "site/public/media/wallpapers/night-drive.jpg").read_bytes()
    sources = {"image": ("image/jpeg",jpg), "gif": ("image/gif",(MEDIA / "startrail.gif").read_bytes()), "video": ("video/mp4",(MEDIA / "motion.mp4").read_bytes())}
    entries = []
    for kind, (mime, data) in sources.items():
        url = base + "/fixture/" + kind
        entries.append(dict(id=kind, kind=kind, title={"en":kind,"zh":kind}, author="Test fixture", width=1920,height=1080,bytes=len(data), sha1=hashlib.sha1(data).hexdigest(), source_url=base, license_url=base,license="Fixture",thumbnail_url=base+"/media/wallpapers/night-drive.webp",download_url=url,tags=[]))
        page.route(url, lambda route,_request,m=mime,d=data:route.fulfill(status=200,content_type=m,body=d))
    page.route("**/desktop/?lang=zh", lambda route:route.fulfill(content_type="text/html",body=(ROOT/"site/public/desktop/index.html").read_text(encoding="utf8")))
    page.route("**/desktop/catalog.json",lambda route:route.fulfill(content_type="application/json",body=json.dumps(entries)))
    page.goto(base+"/desktop/?lang=zh")
    page.click('[data-act="library:online"]')
    for kind in sources:
        page.click(f'[data-type="{kind}"]')
        page.click("[data-preview]")
        page.wait_for_function("""()=>{const e=document.querySelector('.online-player img,.online-player video');return e&&(e.naturalWidth>0||e.currentTime>0)}""")
        page.click("[data-preview]")
        page.click("[data-fetch]")
        expect(page.locator("[data-fetch]")).to_have_text("已下载",timeout=10000)
    page.click("[data-apply]")
    assert page.locator("#movie").get_attribute("src").startswith("blob:")


def desktop_tools(page,base):
    page.goto(base+"/desktop/?lang=en")
    click=lambda name:page.locator('[data-act="'+name+'"]').first.click()
    click("show:tools")
    click("tool:launch")
    page.fill("#lookup","12*(3+4)")
    expect(page.locator("#matches")).to_have_text("12*(3+4) = 84")
    click("tool:home")
    click("tool:files")
    click("inspect:3")
    click("hash:3")
    expect(page.locator("#digest")).to_have_text(hashlib.sha256(b"").hexdigest())
    page.check('[data-batch="3"]')
    click("batch")
    page.fill("#prefix","Meeting")
    click("batch-preview")
    click("batch-apply")
    expect(page.locator("#file-list")).to_contain_text("Meeting-1.txt")
    click("undo-rename")
    click("show:cursor")
    page.click('[data-group=grid] summary')
    page.check('[data-key=collected]')
    page.select_option('[data-key=place]',"top")
    page.locator('#win-actions [data-act=hide]').first.click()
    page.locator("#desk").click(button="right",position={"x":200,"y":90})
    click("newfile")
    expect(page.locator("#loose-files .file-tile")).to_have_count(1)
    page.locator(".collect-tip").click()
    expect(page.locator("#loose-files .file-tile")).to_have_count(0)
    page.clock.install()
    page.locator("#desk").click(button="right",position={"x":200,"y":110})
    click("newfile")
    expect(page.locator("#loose-files .file-tile")).to_have_count(1)
    page.clock.fast_forward(60001)
    expect(page.locator("#loose-files .file-tile")).to_have_count(0)
    page.locator("#files [data-file='3']").click(button="right")
    click("properties:3")
    expect(page.locator("dialog")).to_contain_text("Ideas.txt")
    page.locator("dialog button").click()
    page.locator("[data-file='2']").click()
    expect(page.locator("#file-viewer")).to_be_visible()
    from_rect=page.locator("#file-viewer article").bounding_box()
    page.locator("[data-shortcut]").click()
    click("show:capture")
    click("long")
    expect(page.locator("[data-cmd=smart]")).to_be_visible()
    x,y=round(from_rect["x"]+12),round(from_rect["y"]+25)
    w,h=round(from_rect["width"]-65),round(from_rect["height"]-44)
    page.mouse.move(x,y);page.mouse.down();page.mouse.move(x+w,y+h,steps=4);page.mouse.up()
    page.locator('[data-cmd=longshot]').click()
    expect(page.locator(".long-session")).to_be_visible()
    page.locator("#file-viewer article").evaluate("e=>e.scrollBy(0,126)")
    expect(page.locator(".long-session small")).to_contain_text("2 frames stitched",timeout=10000)
    click("finish")
    assert page.locator(".pinned-view img").evaluate("e=>e.naturalHeight")>h
    page.locator(".native-pin").click(button="right")
    page.click("[data-pin=close]")
    page.locator("[data-shortcut]").click()
    click("show:tools")
    click("newnote")
    page.locator(".note textarea").last.fill("Review by Friday")
    page.locator(".note-head").last.click(button="right")
    click("manage-note:2")
    expect(page.locator("[data-note-body]")).to_have_value("Review by Friday")
    click("archive:2")
    expect(page.locator("#sticky-2")).to_be_hidden()
    click("archives")
    click("manage:2")
    click("archive:2")
    expect(page.locator("#sticky-2")).to_be_visible()
    click("show:capture")
    click("studio")
    page.locator(".studio-picker input").set_input_files(str(MEDIA / "motion.mp4"))
    expect(page.locator(".inline-studio video")).to_be_visible()
    page.wait_for_function("()=>document.querySelector('.inline-studio video')?.videoWidth>0")
    click("close-studio")
    expect(page.locator(".inline-studio")).to_have_count(0)


def actual_catalog(page,base):
    page.goto(base+"/desktop/?lang=en")
    counts=page.evaluate("""async()=>{const entries=await(await fetch('./catalog.json')).json(),counts={};
      for(const entry of entries){if(!entry.cover||!entry.preview||!entry.author||!entry.license_url)throw Error(entry.id);counts[entry.kind]=(counts[entry.kind]||0)+1;}
      return counts;}""")
    assert counts=={"image":52,"gif":20,"video":33}
    total=page.evaluate("""async()=>{const q=await(await fetch('./catalog.json')).json();let i=0,passed=0;
      async function check(){while(i<q.length){const it=q[i++],img=new Image();img.src=it.cover;await img.decode();
        if(it.kind!=='image'){await new Promise((resolve,reject)=>{const v=document.createElement('video');v.muted=true;v.preload='metadata';const timer=setTimeout(()=>reject(Error(it.id)),30000);
          v.onloadedmetadata=()=>{clearTimeout(timer);const ok=v.videoWidth>0&&v.duration>0;v.removeAttribute('src');v.load();ok?resolve():reject(Error(it.id));};v.onerror=()=>{clearTimeout(timer);reject(Error(it.id));};v.src=it.preview;});}
        passed++;}}
      await Promise.all(Array.from({length:4},check));return passed;}""")
    assert total==sum(counts.values())
    page.click('[data-act="library:online"]')
    for kind in counts:
        page.click(f'[data-type="{kind}"]')
        expect(page.locator(".catalog-grid button")).to_have_count(counts[kind])
        page.click("[data-preview]")
        page.wait_for_function("""()=>{const e=document.querySelector('.online-player img,.online-player video');return e&&(e.naturalWidth>0||e.currentTime>0)}""")
        page.click("[data-preview]")


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("base",nargs="?",default="http://127.0.0.1:49173")
    parser.add_argument("--channel",default="chrome")
    parser.add_argument("--direct",action="store_true",help="Bypass the local system proxy when checking a deployment")
    args=parser.parse_args()
    with sync_playwright() as pw:
        browser=pw.chromium.launch(channel=args.channel,args=['--no-proxy-server'] if args.direct else [])
        expect.set_options(timeout=30000 if args.base.startswith('https:') else 7000)
        desktop=browser.new_page(viewport={"width":1366,"height":900},locale="zh-CN")
        desktop.set_default_timeout(30000 if args.base.startswith('https:') else 7000)
        exercise(desktop,args.base)
        fixtures(browser.new_page(),args.base)
        desktop_tools(browser.new_page(viewport={'width':1024,'height':650}),args.base)
        actual_catalog(browser.new_page(),args.base)
        phone=browser.new_page(viewport={"width":375,"height":730},has_touch=True,is_mobile=True,locale="en-US")
        phone.goto(args.base+"/en/",wait_until="domcontentloaded")
        frame=phone.frame_locator("iframe")
        frame.locator('[data-act="show:about"]').tap()
        expect(frame.locator(".about-card")).to_contain_text("v2.0.6")
        assert frame.locator("#content").evaluate("e=>e.scrollWidth<=e.clientWidth+1")
        phone.locator('.lab-cta a').tap()
        expect(phone).to_have_url(args.base+"/en/#download")
        browser.close()
    print("Desktop, mobile, screenshot pixels, scroll stitching, GIF export, tools, 105 previews and three verified transfers passed.")


if __name__=="__main__":
    main()
