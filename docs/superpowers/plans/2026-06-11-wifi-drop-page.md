# WiFi Drop Page Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the bare upload page with the approved drop-zone-first red design.

**Architecture:** Presentation-only — swap the `page[]` raw string in `root_get` (`wifi_drop.cpp`) for the new HTML/CSS/JS. Same endpoints, same XHR upload; adds tap-to-pick via a hidden input + desktop drag-drop, both feeding the same `up(file)`.

**Tech Stack:** `esp_http_server` embedded HTML (self-contained, no external assets).

---

## Conventions

**[BUILD]/[FLASH]:** `$idfp="C:\Users\WilliamHerr\esp\v5.5.2\esp-idf"; $env:IDF_PATH=$idfp; $env:IDF_PYTHON_ENV_PATH="C:\Users\WilliamHerr\.espressif\python_env\idf5.5_py3.11_env"; & "$idfp\export.ps1" *> $null; idf.py -C "...\firmware" build` (flash: `-p COM32 flash`).

## Task 1: The new page

**Files:** Modify `firmware/components/wifi_drop/wifi_drop.cpp` (the `page[]` string inside `root_get`).

- [ ] **Step 1: Replace the page** — in `root_get`, replace the entire `static const char page[] = R"PAGE(...)PAGE";` literal with:

```cpp
    static const char page[] = R"PAGE(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>RSVP Reader</title>
<style>
body{font-family:'Segoe UI',sans-serif;background:#0b0d10;color:#e6e9ef;margin:0;padding:18px;
display:flex;justify-content:center}
#card{width:100%;max-width:430px;background:#12151a;border-radius:14px;padding:20px 16px}
h1{font-size:17px;font-weight:600;margin:0 0 14px}h1 span{font-weight:400;color:#8a93a3}
#dz{border:2px dashed #ff3b3b;border-radius:12px;padding:26px 12px;text-align:center;
background:rgba(255,59,59,.07);cursor:pointer;margin-bottom:16px}
#dz.over{background:rgba(255,59,59,.18)}
#dz .arrow{font-size:26px;color:#ff3b3b;margin-bottom:6px}
#dz .hint{font-size:11px;color:#8a93a3;margin-top:4px}
#st{font-size:11px;color:#8a93a3;margin:0 0 5px;min-height:14px}
#bar{height:5px;background:#1d222a;border-radius:3px;margin-bottom:16px;display:none}
#fill{height:5px;width:0;background:#ff3b3b;border-radius:3px}
#hdr{font-size:10px;color:#8a93a3;letter-spacing:1.2px;margin-bottom:4px}
.b{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:9px 0;
border-bottom:1px solid #1d222a;font-size:13px}
.b:last-child{border-bottom:none}
.b span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.b button{flex:none;background:none;border:1px solid #4a2a2e;border-radius:6px;color:#ff6b6b;
font-size:9px;letter-spacing:.5px;padding:3px 9px;cursor:pointer}
</style></head><body><div id=card>
<h1>RSVP Reader <span>/ drop</span></h1>
<div id=dz><div class=arrow>&#11015;</div>Tap or drop a book here<div class=hint>.epub or .txt</div></div>
<input type=file id=f accept=".epub,.txt" style="display:none">
<p id=st></p><div id=bar><div id=fill></div></div>
<div id=hdr>ON THE CARD</div><div id=lst></div>
</div><script>
var dz=document.getElementById('dz'),fi=document.getElementById('f'),
st=document.getElementById('st'),bar=document.getElementById('bar'),
fill=document.getElementById('fill');
function load(){fetch('/list').then(r=>r.text()).then(t=>{
var names=t.split('\n').filter(x=>x),d=document.getElementById('lst');d.innerHTML='';
document.getElementById('hdr').textContent='ON THE CARD - '+(names.length==1?'1 BOOK':names.length+' BOOKS');
names.forEach(function(n){var e=document.createElement('div');e.className='b';
e.innerHTML='<span></span><button>DELETE</button>';e.children[0].textContent=n;
e.children[1].onclick=function(){if(confirm('Delete '+n+'?'))
fetch('/delete?name='+encodeURIComponent(n),{method:'POST'}).then(load);};d.appendChild(e);});});}
function up(f){if(!f)return;
bar.style.display='block';fill.style.width='0';
st.textContent='Uploading "'+f.name+'"...';
var x=new XMLHttpRequest();x.open('POST','/upload?name='+encodeURIComponent(f.name));
x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(100*e.loaded/e.total);
fill.style.width=p+'%';st.textContent='Uploading "'+f.name+'"... '+p+'%';}};
x.onload=function(){st.textContent=x.status==200?'Done.':'Failed ('+x.status+').';
bar.style.display='none';load();};
x.onerror=function(){st.textContent='Upload error.';bar.style.display='none';};x.send(f);}
dz.onclick=function(){fi.click();};
fi.onchange=function(){up(this.files[0]);this.value='';};
dz.ondragover=function(e){e.preventDefault();dz.classList.add('over');};
dz.ondragleave=function(){dz.classList.remove('over');};
dz.ondrop=function(e){e.preventDefault();dz.classList.remove('over');up(e.dataTransfer.files[0]);};
load();
</script></body></html>)PAGE";
```

- [ ] **Step 2: Build + flash** — **[BUILD]** (expect `Project build complete`), **[FLASH]**.

- [ ] **Step 3: Verify on a phone + desktop** — open WiFi Drop, connect, load `http://192.168.4.1`:
  dark slate card, red dashed drop zone; **tap** the zone → file picker → upload shows the red bar + `Uploading "name"... N%` → "Done." and the list refreshes with `ON THE CARD - N BOOKS`; long titles ellipsize; **DELETE** chip confirms + removes. From a desktop browser on the AP, **drag a file onto the zone** → border highlights, drop uploads.

- [ ] **Step 4: Commit**
```powershell
cd "C:\Users\WilliamHerr\Desktop\Code\RSVP-Reader"; git add firmware/components/wifi_drop/wifi_drop.cpp; git commit -m "feat(firmware): redesigned WiFi Drop page (drop-zone-first, red)"
```
