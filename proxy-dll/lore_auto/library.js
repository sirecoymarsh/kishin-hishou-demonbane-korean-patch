'use strict';
const $=id=>document.getElementById(id);
const bridge=window.chrome?.webview;
// EPUB.js queries <html>, while this EPUB declares vertical writing on <body>.
// Synchronize before its existing layout engine measures and paginates the text.
const epubWritingMode=ePub.Contents.prototype.writingMode;
ePub.Contents.prototype.writingMode=function(mode){
 if(!mode&&this.content){const actual=this.window.getComputedStyle(this.content).writingMode;if(actual.startsWith('vertical'))epubWritingMode.call(this,actual);}
 return epubWritingMode.call(this,mode);
};
let catalog=[],filter='all',active=null,ebook=null,rendition=null,page=0,epoch=0,pending=false,night=false,rtl=false,font=22,noticeTimer;
const FONT_MIN=14,FONT_MAX=72,SPREAD_MIN=1000;  // 11-09 Claude: 800 이면 800x600 창에서 두 쪽(한 단 약 13자) - 양쪽 정렬 단어 사이가 크게 벌어짐
let pageLayout='center-spread',reflowVersion=0,navigationTarget=null;
let shelfCovers={},lastWheelTurn=0,lastFontWheel=0;
async function loadShelfCovers(){
 try{const response=await fetch('covers/index.json',{cache:'no-store'});if(response.ok)shelfCovers=await response.json();}catch{}
 if(!$('library').hidden)drawShelf();
}
const progressKey=id=>'demonbane.library.'+id;
function saved(id){try{return JSON.parse(localStorage.getItem(progressKey(id)))||{};}catch{return {};}}
function remember(data){if(!active)return;try{localStorage.setItem(progressKey(active.id),JSON.stringify({...saved(active.id),...data,readAt:Date.now()}));}catch{notice('읽던 위치를 저장하지 못했습니다. 저장 공간을 확인해 주세요.');}}
function send(message){if(message==='fullscreen'&&!beginViewportChange())return;if(bridge)bridge.postMessage(message);else notice('게임의 도서관 메뉴에서 열어 주세요.');}
function notice(message){$('notice').textContent=message;$('notice').hidden=false;clearTimeout(noticeTimer);noticeTimer=setTimeout(()=>$('notice').hidden=true,6500);}
function text(tag,cls,value){const el=document.createElement(tag);if(cls)el.className=cls;el.textContent=value;return el;}
// 11-19 Claude: a book with no prepared cover (covers/index.json) shows the cover and title written inside its EPUB
const autoMeta={};let autoQueue=Promise.resolve();
function autoCover(b,cover,card){
 const apply=m=>{if(m.title){b.title=m.title;const h=card.querySelector('h2');if(h)h.textContent=m.title.replace(/^참마대성 데몬베인\s*·\s*/,'');}
  if(m.cover&&!cover.querySelector('.cover-art')){const frame=text('div','cover-window',''),img=document.createElement('img');frame.style.setProperty('--cover-aspect',m.aspect||0.7);img.className='cover-art';img.alt=b.title+' 표지';img.decoding='async';img.style.width='100%';img.style.left='0%';img.onload=()=>cover.classList.add('has-art');img.onerror=()=>frame.remove();img.src=m.cover;frame.append(img);cover.append(frame);}};
 if(!autoMeta[b.id])autoMeta[b.id]=autoQueue=autoQueue.then(async()=>{const m={};let book=null;
  try{book=ePub(b.url,{openAs:'epub'});await book.ready;const md=await book.loaded.metadata;if(md&&md.title)m.title=String(md.title).trim();
   const u=await book.coverUrl();if(u){const blob=await (await fetch(u)).blob();m.cover=await new Promise(r=>{const fr=new FileReader();fr.onload=()=>r(fr.result);fr.onerror=()=>r(null);fr.readAsDataURL(blob);});
    if(m.cover){const im=new Image();await new Promise(r=>{im.onload=im.onerror=r;im.src=m.cover;});if(im.naturalWidth&&im.naturalHeight)m.aspect=(im.naturalWidth/im.naturalHeight).toFixed(4);}}}
  catch(e){}finally{try{if(book)book.destroy();}catch(e){}}return m;});
 autoMeta[b.id].then(apply);
}
function drawShelf(){
 const shown=catalog.filter(b=>b.available&&(filter==='all'||b.kind===filter));$('books').replaceChildren();$('count').textContent=shown.length+'권';$('empty').hidden=shown.length>0;
 for(const b of shown){const state=saved(b.id),card=document.createElement('button');card.className='book '+b.kind;card.disabled=!b.available;card.dataset.book=b.index;card.dataset.volume=String(shown.indexOf(b)+1).padStart(2,'0');
  const cover=text('div','book-cover','');cover.append(text('span','book-type',b.kind==='epub'?'NOVEL · 소설':'COMIC · 만화'),text('span','book-symbol',b.kind==='epub'?'Aa':'▤'));
  const art=shelfCovers[b.id];if(art){const frame=text('div','cover-window',''),img=document.createElement('img');frame.style.setProperty('--cover-aspect',art.aspect);img.className='cover-art';img.alt=b.title+' 표지';img.loading='lazy';img.decoding='async';img.style.width=art.width+'%';img.style.left=art.left+'%';img.onload=()=>cover.classList.add('has-art');img.onerror=()=>frame.remove();img.src=art.src;frame.append(img);cover.append(frame);}
  const info=text('div','book-info','');info.append(text('div','series','참마대성 데몬베인'));info.append(text('h2','',b.title.replace(/^참마대성 데몬베인\s*·\s*/,'')));info.append(text('div','progress',!b.available?'폴더를 찾을 수 없습니다':state.readAt?(b.kind==='comic'?'읽던 곳 · '+((state.page||0)+1)+'쪽':'읽던 위치 저장됨'):'읽지 않음'));
  const source=text('div','source',b.path);source.title=b.path;info.append(source);card.append(cover,info,text('span','open-label',state.readAt?'이어서 읽기 →':'읽기 →'));card.onclick=()=>requestBook(b.index);$('books').append(card);if(!art&&b.kind==='epub'&&b.url)autoCover(b,cover,card);
 }
}
function dispose(){clearViewportAnchor();navigationTarget=null;resetIllustrationPair(true);cancelPageCount();++epoch;pending=false;if(rendition){rendition.destroy();rendition=null;}if(ebook){ebook.destroy();ebook=null;}$('epub-view').replaceChildren();$('comic-image').removeAttribute('src');$('illustration').hidden=true;}
function requestBook(index){dispose();active=null;$('library').hidden=true;$('reader').hidden=false;$('loading').hidden=false;$('title').textContent=catalog.find(b=>b.index===index)?.title||'도서관';$('read-state').textContent='불러오는 중';send('open:'+index);}
function shelf(){dispose();active=null;$('reader').hidden=true;$('library').hidden=false;drawShelf();}
function fail(err){console.error(err);$('loading').hidden=true;$('read-state').textContent='불러오기 실패';notice(typeof err==='string'?err:'책을 읽지 못했습니다. 폴더 구조와 EPUB 파일을 확인해 주세요.');}

// One shared route for mouse wheels in the page, iframe and illustration overlay.
function wheelPixels(e){return e.deltaY*(e.deltaMode===1?32:e.deltaMode===2?innerHeight:1);}
function turnWheel(e){
 if(!active||e.ctrlKey||e.metaKey||!e.deltaY)return;
 e.preventDefault();e.stopPropagation();
 const now=performance.now();if(pending||!$('loading').hidden||now-lastWheelTurn<220)return;
 lastWheelTurn=now;move(e.deltaY>0?1:-1);
}
function fontWheel(e){
 e.preventDefault();e.stopPropagation();if(!e.deltaY||!rendition)return;
 const now=performance.now();if(now-lastFontWheel<100)return;lastFontWheel=now;
 setFont(font+(e.deltaY<0?2:-2));
}
// 11-09 Claude (사용자 「그림이 2개 있는 페이지에서 멋대로 휠이 페이지 넘기기에서 그림 확대축소로 전환되네」「각 장 시작도」): in the reading
// view the wheel always turns pages - over an illustration it used to open the zoom view instead, and two-picture pages and chapter
// openings are almost all picture. Zoom = click a picture (its own view, where the wheel zooms).
// 11-09 Claude (사용자 「일러스트를 컨트롤을 눌렀을때만 휠로 확대축소 하게 하고 평상시엔 그냥 화면 가득차는걸 기본으로」): Ctrl+wheel over a
// picture zooms it (the zoom view opens at that spot), over text it changes the type size; it never reaches WebView2's own page zoom
function contentWheel(e,contents){
 if(e.ctrlKey||e.metaKey){
  e.preventDefault();e.stopPropagation();
  const img=e.target.closest?.('img');
  if(!img){fontWheel(e);return;}
  const w=paintedWheel(img,e);if(w)illustration(img.src,w);
  return;
 }
 turnWheel(e);
}
// Ctrl+wheel up over a picture (an object-fit:contain picture: where it is painted inside its box): the zoom view opens at fit (the whole
// screen - 「평상시엔 그냥 화면 가득차는걸 기본으로」) and zooms in from there around the spot pointed at (u,v = where on the picture, 0..1).
// 11-09 둘째: it used to start from the picture's size on the page, so over one picture of a two-picture spread it opened smaller than
// fit and off to one side. Ctrl+wheel down over a picture does nothing (it is already whole).
function paintedWheel(img,e){
 const delta=wheelPixels(e);if(delta>=0||!img.naturalWidth)return null;
 const r=img.getBoundingClientRect(),s=Math.min(r.width/img.naturalWidth,r.height/img.naturalHeight),w=img.naturalWidth*s,h=img.naturalHeight*s;
 const clamp=v=>Math.max(0,Math.min(1,v));
 return {u:clamp((e.clientX-r.left-(r.width-w)/2)/w),v:clamp((e.clientY-r.top-(r.height-h)/2)/h),delta};
}
// the plain wheel in a zoom view: the previous/next picture (or comic page), at the same pace as page turns
function stepWheel(e,step){
 e.preventDefault();e.stopPropagation();
 const now=performance.now();if(!e.deltaY||pending||now-lastWheelTurn<220)return;
 lastWheelTurn=now;step(e.deltaY>0?1:-1);
}
function visibleEpubImage(){
 if(illustrationPair||illustrationSingle)return $('paired-illustrations')?.querySelector('img');
 if(!rendition)return null;const bounds=$('epub-view').getBoundingClientRect();let best=null,area=0;
 for(const contents of rendition.getContents()){
  const frame=contents.document.defaultView.frameElement;if(!frame)continue;const f=frame.getBoundingClientRect();
  for(const img of contents.document.images){if(img.naturalWidth<100||img.naturalHeight<100)continue;const r=img.getBoundingClientRect();
   const overlap=Math.max(0,Math.min(bounds.right,f.left+r.right)-Math.max(bounds.left,f.left+r.left))*Math.max(0,Math.min(bounds.bottom,f.top+r.bottom)-Math.max(bounds.top,f.top+r.top));
   if(overlap>area){area=overlap;best=img;}
  }
 }
 return best;
}
function syncImageNav(){
 // 11-09: one picture at a time - the left of a two-picture spread can still go on to its right one (and back)
 const p=illustrationPair,onLeft=!!p&&zoomSrc===p.left.src,onRight=!!p&&zoomSrc===p.right.src;
 $('image-prev').disabled=$('prev').disabled&&!onRight;$('image-next').disabled=$('next').disabled&&!onLeft;
 $('image-location').textContent=$('location').textContent;
}
// 11-09 Claude (사용자 「일러스트가 2개 있는 페이지는 누르면 일러스트 2개가 한꺼번에 확대 화면에 들어가는데 누르면 각자의 확대화면으로 들어가게 해다오」):
// the zoom view shows the one picture clicked; its ← → (buttons and keys) step picture by picture - a spread's left, its right, the next page
let zoomSrc=null;
async function stepIllustration(dir){
 const p=illustrationPair;
 if(p&&dir>0&&zoomSrc===p.left.src){await showIllustration(p.right.src);return;}
 if(p&&dir<0&&zoomSrc===p.right.src){await showIllustration(p.left.src);return;}
 await move(dir);
}
function closeIllustration(){$('illustration').hidden=true;}
class ZoomImage{
 constructor(stage,img){this.stage=stage;this.img=img;this.scale=1;this.x=this.y=0;this.w=this.h=0;this.drag=null;
  // 11-09: zoom only with Ctrl+wheel; the plain wheel goes to the previous/next picture (comic: page) - fit is the default view
  stage.addEventListener('wheel',e=>{if(e.ctrlKey||e.metaKey){e.preventDefault();e.stopPropagation();this.zoom(e.clientX,e.clientY,Math.exp(-wheelPixels(e)*.0015));return;}
   stepWheel(e,stage.id==='comic-view'?move:stepIllustration);},{passive:false});
  stage.addEventListener('pointerdown',e=>{if(e.button!==0)return;this.drag={x:e.clientX,y:e.clientY,ox:this.x,oy:this.y};stage.setPointerCapture(e.pointerId);});
  stage.addEventListener('pointermove',e=>{if(!this.drag)return;this.x=this.drag.ox+e.clientX-this.drag.x;this.y=this.drag.oy+e.clientY-this.drag.y;this.paint();});
  stage.addEventListener('pointerup',()=>this.drag=null);stage.addEventListener('pointercancel',()=>this.drag=null);stage.addEventListener('dblclick',()=>this.fit());
  new ResizeObserver(()=>{if(!stage.hidden&&this.w)this.fit();}).observe(stage);
 }
 loaded(){this.w=this.img.naturalWidth;this.h=this.img.naturalHeight;this.fit();}
 fit(){if(!this.w||!this.stage.clientWidth||!this.stage.clientHeight)return;this.base=Math.min(this.stage.clientWidth/this.w,this.stage.clientHeight/this.h);this.scale=this.base;this.x=(this.stage.clientWidth-this.w*this.scale)/2;this.y=(this.stage.clientHeight-this.h*this.scale)/2;this.paint();}
 zoom(cx,cy,factor){if(!this.w)return;const r=this.stage.getBoundingClientRect(),px=cx-r.left,py=cy-r.top,next=Math.max(this.base*.5,Math.min(this.base*12,this.scale*factor));this.x=px-(px-this.x)*next/this.scale;this.y=py-(py-this.y)*next/this.scale;this.scale=next;this.paint();}
 paint(){this.img.style.width=this.w+'px';this.img.style.height=this.h+'px';this.img.style.transform=`translate(${this.x}px,${this.y}px) scale(${this.scale})`;}
}
const comicZoom=new ZoomImage($('comic-view'),$('comic-image')),imageZoom=new ZoomImage($('image-stage'),$('large-image')),nextImageZoom=new ZoomImage($('image-stage-next'),$('large-image-next'));
// Reuse EPUB.js section rendering and the existing zoom/navigation path for image pairs.
let illustrationPair=null,pairKey='',pairTask=Promise.resolve(),pairEpoch=0;
const imageSections=new Map();
// 11-09 Claude (사용자 「일러스트 페이지는 문서 페이지와 별개로 확대 축소가 적용되게 해줄래?」「맨 처음 클릭했을때 뜨는 표지가 화면 전체에 차게」):
// a picture-only view - a two-picture spread, or one picture page shown by itself (the cover when a book opens, a picture page in
// one-page view) - is drawn over the whole reading area (#reading-body), not inside the text page frame (max 1360/860 px), as big as it fits.
let illustrationSingle=null;
const coverSections=new Map();
function resetIllustrationPair(clearCache=false){
 ++pairEpoch;pairKey='';illustrationPair=null;illustrationSingle=null;
 $('paired-illustrations')?.remove();$('epub-view').classList.remove('has-illustration-pair');
 if(clearCache){imageSections.clear();coverSections.clear();}
}
// the cover (body p-cover) is not a p-image page, so it never joins a two-picture pair - only shown alone, full size
function coverSection(index,book=ebook){
 if(!book||index<0||index>=book.spine.length)return Promise.resolve(null);
 if(coverSections.has(index))return coverSections.get(index);
 const promise=(async()=>{
  const section=book.spine.get(index),xml=await section.render(book.load.bind(book));
  const doc=new DOMParser().parseFromString(xml,'application/xhtml+xml'),body=doc.querySelector('body'),imgs=body?.querySelectorAll('img');
  if(!body?.classList.contains('p-cover')||imgs.length!==1||body.textContent.trim())return null;
  const raw=imgs[0].getAttribute('src');
  const src=/^(blob:|data:|https?:)/.test(raw)?raw:new URL(raw,new URL(section.url,book.url.toString())).href;
  return {index,href:section.href,src};
 })();coverSections.set(index,promise);return promise;
}
function placeIllustrations(holder=$('paired-illustrations')){if(holder)holder.style.left=$('toc').hidden?'0px':$('toc').offsetWidth+'px';}
function imageSection(index,book=ebook){
 if(!book||index<0||index>=book.spine.length)return Promise.resolve(null);
 if(imageSections.has(index))return imageSections.get(index);
 const promise=(async()=>{
  const section=book.spine.get(index),xml=await section.render(book.load.bind(book));
  const doc=new DOMParser().parseFromString(xml,'application/xhtml+xml'),body=doc.querySelector('body'),imgs=body?.querySelectorAll('img');
  if(!body?.classList.contains('p-image')||imgs.length!==1||body.textContent.trim())return null;
  const raw=imgs[0].getAttribute('src');
  const src=/^(blob:|data:|https?:)/.test(raw)?raw:new URL(raw,new URL(section.url,book.url.toString())).href;
  return {index,href:section.href,src};
 })();imageSections.set(index,promise);return promise;
}
function refreshIllustrationPair(loc=rendition?.currentLocation()){
 if(!loc?.start||!rendition)return Promise.resolve();
 const book=ebook,key=[epoch,loc.start.index,loc.end?.index,pageLayout,rendition.layout().divisor].join(':');
 if(pairKey===key)return pairTask;
 resetIllustrationPair();pairKey=key;const version=pairEpoch;
 pairTask=(async()=>{
  const index=loc.start.index,alone=(loc.end?.index??index)===index;
  let items=null;
  if(['spread','center-spread'].includes(pageLayout)&&rendition.layout().divisor===2&&await imageSection(index,book)){
   let start=index;while(start>0&&await imageSection(start-1,book))--start;
   const leftIndex=start+Math.floor((index-start)/2)*2;
   const left=await imageSection(leftIndex,book),right=await imageSection(leftIndex+1,book);
   if(left&&right)items=[left,right];
  }
  if(!items&&alone){const one=await imageSection(index,book)||await coverSection(index,book);if(one)items=[one];}
  if(version!==pairEpoch||book!==ebook||!items)return;
  const holder=document.createElement('div');holder.id='paired-illustrations';holder.classList.toggle('single',items.length===1);
  holder.setAttribute('aria-label',items.length===2?'연속 삽화 두 쪽':'삽화');
  for(const item of items){const img=document.createElement('img');img.src=item.src;img.alt='삽화 '+(item.index+1);img.draggable=false;img.onclick=()=>illustration(item.src);
   img.style.cursor='zoom-in';holder.append(img);}   // 11-09: no wheel zoom here - the wheel turns pages; click zooms
  holder.addEventListener('wheel',e=>{
   if(e.ctrlKey||e.metaKey){e.preventDefault();e.stopPropagation();const img=e.target.closest?.('img'),w=img&&paintedWheel(img,e);if(w)illustration(img.src,w);return;}
   turnWheel(e);},{passive:false});
  await Promise.all([...holder.children].map(img=>img.decode()));
  if(version!==pairEpoch||book!==ebook)return;
  if(items.length===2)illustrationPair={left:items[0],right:items[1]};else illustrationSingle=items[0];
  placeIllustrations(holder);$('reading-body').append(holder);$('epub-view').classList.add('has-illustration-pair');
  if(items.length===2){$('prev').disabled=items[0].index===0;$('next').disabled=items[1].index===book.spine.length-1;}
  updateBookPosition();syncImageNav();
 })().catch(e=>{if(version===pairEpoch)console.error(e);});
 return pairTask;
}
async function showIllustration(src,wheel){
 await refreshIllustrationPair();
 const pair=null;zoomSrc=src;   // 11-09: always the one picture (a two-picture spread used to open both)
 $('image-stage-next').hidden=!pair;$('image-spread').classList.toggle('paired',!!pair);
 $('large-image').src=pair?pair.left.src:src;
 if(pair)$('large-image-next').src=pair.right.src;else $('large-image-next').removeAttribute('src');
 $('illustration').hidden=false;syncImageNav();
 try{await Promise.all([$('large-image').decode(),...(pair?[$('large-image-next').decode()]:[])]);
  // a wheel zoom waits for the view's first size to land (its ResizeObserver fits the picture and would undo the zoom)
  if(wheel&&!pair)await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));
  imageZoom.loaded();if(pair)nextImageZoom.loaded();
  if(wheel&&!pair){const r=$('image-stage').getBoundingClientRect(),z=imageZoom;z.zoom(r.left+z.x+wheel.u*z.w*z.scale,r.top+z.y+wheel.v*z.h*z.scale,Math.exp(-wheel.delta*.0015));}
 }catch{notice('삽화를 열지 못했습니다.');}
}
// Plain wheel zoom uses the same relative point and scale on both visible illustrations.
for(const [one,other] of [[imageZoom,nextImageZoom],[nextImageZoom,imageZoom]]){
 const zoom=one.zoom.bind(one);one.zoom=(x,y,factor)=>{zoom(x,y,factor);if($('image-stage-next').hidden)return;const a=one.stage.getBoundingClientRect(),b=other.stage.getBoundingClientRect();ZoomImage.prototype.zoom.call(other,b.left+(x-a.left)/a.width*b.width,b.top+(y-a.top)/a.height*b.height,factor);};
}

async function comicPage(n){
 if(!active||active.kind!=='comic')return;const ticket=++epoch;page=Math.max(0,Math.min(active.pages.length-1,n));$('loading').hidden=false;const pic=$('comic-image');pic.src=active.pages[page];pic.alt=active.title+' · '+(page+1)+'쪽';
 try{await pic.decode();if(ticket!==epoch)return;comicZoom.loaded();$('loading').hidden=true;$('page-number').value=page+1;$('page-number').max=active.pages.length;$('page-total').textContent='/ '+active.pages.length+'쪽';$('location').textContent=decodeURIComponent(active.pages[page].split('/').pop());$('read-state').textContent='읽던 위치 자동 저장';$('prev').disabled=page===0;$('next').disabled=page===active.pages.length-1;remember({page,rtl});}catch(e){if(ticket===epoch)fail('이 페이지 이미지를 읽지 못했습니다. 파일 수정이 끝난 뒤 새로고침해 주세요.');}
}
function applyReaderTheme(reader){reader.themes.default({body:{'color':night?'#e9e4da':'#29251e','background':night?'#18191e':'#f2eddf','font-family':'"Noto Serif KR", "Malgun Gothic", serif','line-height':'1.85'},p:{'line-height':'1.85'},img:{'max-width':'100%','max-height':'95vh'},'body.p-image':{'column-width':'auto !important','column-count':'1 !important'},'body.p-image > div':{'width':'100%','height':'100%','display':'flex','align-items':'center','justify-content':'center'},'body.p-image p, body.p-image a, body.p-image span':{'display':'contents'},'body.p-image img':{'width':'auto !important','height':'auto !important','max-width':'100% !important','max-height':'100% !important','object-fit':'contain','margin':'auto'},a:{color:night?'#cdb67c':'#705e34'}});reader.themes.fontSize(font+'px');}
function applyTheme(){if(!rendition)return;applyReaderTheme(rendition);syncReadingControls();$('theme').textContent=night?'종이 모드':'야간 모드';$('epub-view').classList.toggle('night',night);remember({font,night,pageLayout});}
function syncReadingControls(){
 $('font-size').value=font;$('font-down').disabled=font<=FONT_MIN;$('font-up').disabled=font>=FONT_MAX;
 $('page-layout').value=pageLayout;
}
function setPageFrame(){
 resetIllustrationPair();
 for(const target of [$('epub-view'),$('illustration')]){target.classList.toggle('centered-page',pageLayout==='center');target.classList.toggle('centered-spread',pageLayout==='center-spread');}
 $('reading-body').classList.toggle('paper-night',night);
}
async function reflowReading(){
 const reader=rendition,ticket=epoch,version=++reflowVersion;if(!reader)return;
 const cfi=reader.location?.start?.cfi;
 setPageFrame();applyTheme();
 const spread=['spread','center-spread'].includes(pageLayout)?'auto':'none';
 reader.settings.spread=spread;
 reader.layout({...reader.layout().settings,spread,minSpreadWidth:SPREAD_MIN});
 resizeReading(cfi);
 schedulePageCount();
 // Let font metrics and the new column geometry settle before restoring the text anchor.
 await Promise.all(reader.getContents().map(c=>c.document.fonts.ready));
 await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
 if(version!==reflowVersion||ticket!==epoch||reader!==rendition)return;
 try{if(cfi)await reader.display(cfi);schedulePageCount();}catch(e){if(ticket===epoch)fail(e);}
}
function setFont(value){
 const next=Math.max(FONT_MIN,Math.min(FONT_MAX,Math.round(Number(value)||font)));
 if(next===font){syncReadingControls();return;}font=next;syncReadingControls();reflowReading();
}

// Native viewport anchor preservation: pagination can move the first visible
// CFI backwards on a wide spread. Keep the actual reading point until the user
// navigates, instead of using that new page boundary on the return resize.
let viewportAnchor=null,viewportSettling=false,viewportTimer,viewportVersion=0;
function clearViewportAnchor(){clearTimeout(viewportTimer);++viewportVersion;viewportAnchor=null;viewportSettling=false;}
function beginViewportChange(){
 if(!rendition||!active||active.kind!=='epub')return true;
 if(viewportSettling)return false;
 viewportAnchor=viewportAnchor||rendition.location?.start?.cfi;
 viewportSettling=!!viewportAnchor;
 if(viewportSettling)scheduleViewportAnchor();
 return true;
}
function scheduleViewportAnchor(){
 clearTimeout(viewportTimer);
 const reader=rendition,ticket=epoch,version=++viewportVersion,anchor=viewportAnchor;
 if(!reader||!anchor||!viewportSettling)return;
 viewportTimer=setTimeout(async()=>{
  const current=()=>ticket===epoch&&reader===rendition&&version===viewportVersion;
  try{
   if(!current())return;
   resizeReading(anchor);
   await Promise.all(reader.getContents().map(c=>c.document.fonts.ready));
   await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
   if(!current())return;
   await reader.display(anchor);
   await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
   if(!current())return;
   viewportSettling=false;
   remember({cfi:anchor,href:reader.location?.start?.href});schedulePageCount();
  }catch(e){if(current()){viewportSettling=false;fail(e);}}
 },200);
}

function resizeReading(cfi){
 if(!rendition||$('epub-view').hidden)return;
 const view=$('epub-view');if(view.clientWidth&&view.clientHeight)rendition.resize(view.clientWidth,view.clientHeight,navigationTarget||cfi);
}
new ResizeObserver(()=>{resizeReading(viewportSettling?viewportAnchor:undefined);if(viewportSettling)scheduleViewportAnchor();schedulePageCount();}).observe($('epub-view'));
let pageCounts=null,countReader=null,countTimer,countVersion=0;
const pageCountCache=new Map();
function cancelPageCount(){
 ++countVersion;clearTimeout(countTimer);pageCounts=null;
 // Stop EPUB.js's queued start before destroy clears its book reference.
 if(countReader){countReader.q.stop();countReader.destroy();countReader=null;}
 $('page-counter')?.remove();
}
function updateBookPosition(loc=rendition?.location){
 if(!loc?.start)return;
 if(!pageCounts){$('location').textContent='책 전체 쪽 수 계산 중…';return;}
 const offset=index=>pageCounts.slice(0,index).reduce((sum,n)=>sum+n,0);
 const first=illustrationPair?offset(illustrationPair.left.index)+1:offset(loc.start.index)+Math.min(loc.start.displayed.page,pageCounts[loc.start.index]);
 const last=illustrationPair?offset(illustrationPair.right.index)+1:offset(loc.end.index)+Math.min(loc.end.displayed.page,pageCounts[loc.end.index]);
 const total=pageCounts.reduce((sum,n)=>sum+n,0);
 $('location').textContent=(last>first?first+'–'+last:first)+' / '+total+'쪽';
 syncImageNav();
}
function schedulePageCount(){
 if(!rendition||!active||active.kind!=='epub')return;
 cancelPageCount();updateBookPosition();
 const ticket=countVersion;
 countTimer=setTimeout(()=>countBookPages(ticket),350);
}
async function countBookPages(ticket){
 if(ticket!==countVersion||!rendition?.location)return;
 const book=ebook,main=rendition,layout=main.layout();
 const key=[active.id,font,layout.width,layout.height,layout.divisor,layout.gap].join(':');
 if(pageCountCache.has(key)){pageCounts=pageCountCache.get(key);updateBookPosition();return;}
 const host=document.createElement('div');host.id='page-counter';host.setAttribute('aria-hidden','true');
 Object.assign(host.style,{position:'fixed',left:'-100000px',top:'0',width:layout.width+'px',height:layout.height+'px',opacity:'0',pointerEvents:'none'});
 document.body.append(host);
 const counter=new ePub.Rendition(book,{width:layout.width,height:layout.height,spread:layout.divisor===2?'auto':'none',minSpreadWidth:SPREAD_MIN,flow:'paginated',allowScriptedContent:false,allowPopups:false});
 countReader=counter;counter.attachTo(host);applyReaderTheme(counter);
 const counts=[];
 try{
  for(const section of book.spine.spineItems){
   if(ticket!==countVersion)return;
   await counter.display(section.href);
   if(ticket!==countVersion)return;
   await Promise.all(counter.getContents().map(c=>c.document.fonts.ready));
   await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
   if(ticket!==countVersion)return;
   const loc=await counter.currentLocation();
   if(!loc?.start||loc.start.index!==section.index)throw new Error('Page count section mismatch');
   counts[section.index]=counter.getContents()[0]?.document.body.classList.contains('p-image')?1:loc.start.displayed.total;
  }
  if(ticket!==countVersion)return;
  pageCounts=counts;pageCountCache.set(key,counts);
  if(pageCountCache.size>6)pageCountCache.delete(pageCountCache.keys().next().value);
  updateBookPosition();
 }catch(e){if(ticket===countVersion){console.error(e);$('location').textContent='전체 쪽 수 계산 실패 · 새로고침해 주세요';}}
 finally{if(countReader===counter){counter.destroy();countReader=null;host.remove();}}
}
// 11-09 Claude (사용자 「서고에서 마우스 우클릭으로 이전 메뉴로 나갈수 있게 해줘」): one step back, for Esc and the right button alike -
// the zoom view closes, the table of contents closes, a book goes back to the shelf, the shelf back to the game's 감상실
function backOne(){if(!$('illustration').hidden)closeIllustration();else if(!$('toc').hidden)toggleToc(false);else if(active)shelf();else send('back');}
function rightBack(e){if(e.target?.closest?.('input,textarea,select'))return;e.preventDefault();e.stopPropagation();backOne();}
// 11-09 Claude (사용자 「홈, 엔드 키를 적용시켜」): Home = the book's first page, End = its last
async function goEdge(dir){
 if(!active||pending||viewportSettling||!$('loading').hidden)return;
 if(!$('illustration').hidden)closeIllustration();
 if(active.kind==='comic'){await comicPage(dir<0?0:active.pages.length-1);return;}
 if(!rendition||!ebook)return;
 clearViewportAnchor();const ticket=epoch;pending=true;
 try{
  const items=ebook.spine.spineItems;await rendition.display((dir<0?items[0]:items[items.length-1]).href);
  for(let i=0;dir>0&&i<60&&ticket===epoch;i++){const loc=rendition.currentLocation();if(!loc?.start||rendition.location?.atEnd)break;const before=loc.start.cfi;await rendition.next();if(rendition.currentLocation()?.start?.cfi===before)break;}
  if(ticket===epoch)await refreshIllustrationPair(rendition.currentLocation());
 }catch(e){if(ticket===epoch)console.error(e);}finally{pending=false;}
}
function key(e){
 if(e.key==='F11'||(e.key==='Enter'&&e.altKey)){e.preventDefault();send('fullscreen');return;}
 if(e.key==='Escape'){e.preventDefault();backOne();return;}
 if(!active||(['INPUT','TEXTAREA','SELECT'].includes(e.target?.tagName)||e.target?.isContentEditable)||e.ctrlKey||e.metaKey||e.altKey)return;
 if(e.key==='ArrowRight'||e.key==='ArrowLeft'||e.key==='PageDown'||e.key==='PageUp'){e.preventDefault();let dir=(e.key==='ArrowRight'||e.key==='PageDown')?1:-1;if(active.kind==='comic'&&rtl&&(e.key==='ArrowRight'||e.key==='ArrowLeft'))dir=-dir;if(active.kind!=='comic'&&!$('illustration').hidden)stepIllustration(dir);else move(dir);}
 if(e.key==='Home'||e.key==='End'){e.preventDefault();goEdge(e.key==='Home'?-1:1);}
}
async function illustration(src,wheel){return showIllustration(src,wheel);}

function tocTarget(href){
 const candidates=[href];for(const nav of [ebook.packaging.navPath,ebook.packaging.ncxPath])if(nav){const u=new URL(href,new URL(nav,'https://epub.invalid/'));candidates.push(u.pathname.slice(1)+u.hash);}
 return candidates.find(path=>ebook.spine.get(path.split('#')[0]))||href;
}
async function navigateToc(target){
 const reader=rendition,ticket=epoch;if(!reader)return;
 clearViewportAnchor();navigationTarget=target;toggleToc(false,target);
 try{
  // Closing the side panel changes column width. Its ResizeObserver must use
  // the requested chapter, not queue a redisplay of the previous chapter.
  await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
  if(ticket!==epoch||reader!==rendition)return;
  await reader.display(target);
  await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
 }catch(e){if(ticket===epoch)fail(e);}
 finally{if(ticket===epoch){navigationTarget=null;schedulePageCount();}}
}
function tocItems(items,level=0){for(const item of items){const button=text('button',level?'sub':'',item.label.trim()||'본문');button.onclick=async()=>{await navigateToc(tocTarget(item.href));};$('toc').append(button);if(item.subitems)tocItems(item.subitems,level+1);}}
async function openEpub(data){
 const ticket=epoch,state=saved(data.id);font=Math.max(FONT_MIN,Math.min(FONT_MAX,Number(state.font)||22));night=!!state.night;pageLayout=['wide','spread','center-spread','center'].includes(state.pageLayout)?state.pageLayout:'center-spread';setPageFrame();syncReadingControls();$('toc').replaceChildren();toggleToc(false);
 ebook=ePub(data.url,data.packed?{openAs:'epub'}:{});const book=ebook;let restoring=!!state.cfi,resumeCfi=state.cfi;
 try{
  await book.opened;if(ticket!==epoch)return;
  book.spine.hooks.content.register(doc=>{doc.querySelectorAll('script').forEach(s=>s.remove());});
  rendition=book.renderTo('epub-view',{width:'100%',height:'100%',spread:['spread','center-spread'].includes(pageLayout)?'auto':'none',minSpreadWidth:SPREAD_MIN,flow:'paginated',allowScriptedContent:false,allowPopups:false});
  rendition.hooks.content.register(contents=>{
   contents.document.addEventListener('keydown',key);
   contents.document.addEventListener('wheel',e=>contentWheel(e,contents),{passive:false});
   contents.document.addEventListener('contextmenu',rightBack);   // 11-09: right-click inside the page = one step back
   contents.document.addEventListener('mousedown',middleDown);contents.document.addEventListener('auxclick',middleClick);   // 11-09: middle click = full screen
   for(const img of contents.document.images){img.style.cursor='zoom-in';img.addEventListener('click',e=>{e.preventDefault();e.stopPropagation();illustration(img.src);});}
  });
  applyTheme();
  rendition.on('relocated',loc=>{if(ticket!==epoch||!active)return;if(!restoring&&!viewportSettling)remember({cfi:viewportAnchor||loc.start?.cfi,href:loc.start?.href});refreshIllustrationPair(loc);updateBookPosition(loc);$('epub-view').classList.toggle('is-spread',rendition.layout().divisor===2);$('read-state').textContent=['spread','center-spread'].includes(pageLayout)&&rendition.layout().divisor===1?'좁은 화면에서는 한 쪽 표시':'읽던 위치 자동 저장';$('prev').disabled=!!loc.atStart;$('next').disabled=!!loc.atEnd;syncImageNav();});
  rendition.on('displayError',e=>{if(ticket===epoch)fail(e);});
  try{await rendition.display(state.cfi||undefined);}catch{resumeCfi=null;await rendition.display();notice('책 내용이 바뀌어 처음부터 열었습니다.');}
  if(ticket!==epoch)return;
  // The first iframe layout can precede its final font/column geometry. A CFI
  // at a column boundary otherwise reopens one spread early (observed W6/W7).
  resizeReading(resumeCfi);
  await Promise.all(rendition.getContents().map(c=>c.document.fonts.ready));
  await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
  if(ticket!==epoch)return;
  if(resumeCfi)await rendition.display(resumeCfi);
  await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
  if(ticket!==epoch)return;restoring=false;
  const restored=rendition.currentLocation();remember({cfi:restored.start?.cfi,href:restored.start?.href});
  $('loading').hidden=true;schedulePageCount();const navigation=await book.loaded.navigation;if(ticket!==epoch)return;tocItems(navigation.toc);if(!navigation.toc.length)$('toc').append(text('p','muted','이 책에는 목차가 없습니다.'));
 }catch(e){if(ticket===epoch)fail(e);}
}
function toggleToc(force,target){const show=force??$('toc').hidden;$('toc').hidden=!show;$('toc-toggle').setAttribute('aria-expanded',String(show));placeIllustrations();if(rendition){if(target)rendition.resize(undefined,undefined,target);else setTimeout(()=>rendition&&rendition.resize(),0);}}
async function move(dir){
 if(viewportSettling||pending||!active||!$('loading').hidden||(dir<0?$('prev').disabled:$('next').disabled))return;
 clearViewportAnchor();const ticket=epoch,zoomed=!$('illustration').hidden;pending=true;
 try{
  if(active.kind==='comic'){await comicPage(page+dir);return;}
  if(!rendition)return;await refreshIllustrationPair(rendition.currentLocation());
  const pair=illustrationPair;
  if(pair&&dir>0){const next=ebook.spine.get(pair.right.index+1);if(next)await rendition.display(next.href);}
  else if(pair&&dir<0){await rendition.display(pair.left.href);await rendition.prev();}
  else await(dir>0?rendition.next():rendition.prev());
  await refreshIllustrationPair(rendition.currentLocation());
  if(ticket!==epoch)return;
  if(zoomed&&!$('illustration').hidden){   // 11-09: on a two-picture spread show its left going forward, its right coming back
   const p=illustrationPair,src=p?(dir<0?p.right.src:p.left.src):visibleEpubImage()?.src;if(src)await illustration(src);else closeIllustration();}
  syncImageNav();
 }catch(e){if(ticket===epoch)fail(e);}finally{pending=false;}
}
function opened(data){active=data;const known=catalog.find(b=>b.id===data.id);if(known&&known.title)data.title=known.title;$('title').textContent=data.title;$('kind').textContent=data.kind==='epub'?'NOVEL · 소설':'COMIC · 만화';const comic=data.kind==='comic';$('comic-view').hidden=!comic;$('epub-view').hidden=comic;$('comic-tools').hidden=!comic;$('epub-tools').hidden=comic;$('page-jump').hidden=!comic;$('toc').hidden=true;$('location').textContent='';$('prev').disabled=$('next').disabled=true;
 if(comic){rtl=!!saved(data.id).rtl;updateDirection();comicPage(saved(data.id).page||0);$('comic-view').focus();}else openEpub(data);
}
function updateDirection(){$('direction').textContent='읽기 방향: '+(rtl?'오른쪽 → 왼쪽':'왼쪽 → 오른쪽');}
if(bridge)bridge.addEventListener('message',e=>{const data=e.data;if(data.type==='catalog'){catalog=data.books;drawShelf();const resume=sessionStorage.getItem('refresh-book');if(resume!==null){sessionStorage.removeItem('refresh-book');if(catalog.some(b=>b.index===Number(resume)))requestBook(Number(resume));}}else if(data.type==='opened')opened(data);else if(data.type==='error')fail(data.message);else if(data.type==='notice')notice(data.message);});
$('theater').onclick=()=>send('theater');$('exit').onclick=()=>send('back');document.querySelectorAll('.fullscreen').forEach(b=>b.onclick=()=>send('fullscreen'));
$('add-folder').onclick=()=>send('folder');$('add-epub').onclick=()=>send('import');$('rescan').onclick=()=>send('refresh');$('shelf-back').onclick=shelf;
document.querySelectorAll('[data-filter]').forEach(b=>b.onclick=()=>{filter=b.dataset.filter;document.querySelectorAll('[data-filter]').forEach(x=>x.setAttribute('aria-pressed',String(x===b)));drawShelf();});
$('reload').onclick=()=>{if(active){sessionStorage.setItem('refresh-book',String(active.index));send('refresh');}};$('prev').onclick=()=>move(-1);$('next').onclick=()=>move(1);$('toc-toggle').onclick=()=>toggleToc();
$('font-down').onclick=()=>setFont(font-2);$('font-up').onclick=()=>setFont(font+2);
$('font-size').onchange=()=>setFont($('font-size').value);$('font-size').onkeydown=e=>{if(e.key==='Enter'){$('font-size').blur();setFont($('font-size').value);}};
$('page-layout').onchange=()=>{pageLayout=$('page-layout').value;reflowReading();};
$('theme').onclick=()=>{night=!night;setPageFrame();applyTheme();refreshIllustrationPair();};
$('fit').onclick=()=>comicZoom.fit();$('direction').onclick=()=>{rtl=!rtl;updateDirection();remember({rtl});};$('go-page').onclick=()=>comicPage((parseInt($('page-number').value,10)||1)-1);$('page-number').onkeydown=e=>{if(e.key==='Enter')$('go-page').click();};
$('image-close').onclick=closeIllustration;
// 11-09 Claude (사용자 「그림 확대 페이지에서 마우스 우클릭하면 취소되게 해줘」): right-click anywhere in the zoom view closes it, as 닫기 · Esc
$('illustration').addEventListener('contextmenu',e=>{e.preventDefault();e.stopPropagation();closeIllustration();});
document.addEventListener('contextmenu',rightBack);   // 11-09: the shelf -> 감상실, a book -> the shelf (as Esc)
$('image-fit').onclick=()=>{imageZoom.fit();nextImageZoom.fit();};   // 11-09 둘째: this statement had slid behind the comment above (화면에 맞춤 did nothing)
// 11-09 Claude (사용자 「가운데 휠 클릭으로 서고에서 전체화면 전환도 넣어줘」): the middle button toggles full screen anywhere in LORE -
// the shelf, a book, the zoom view - as F11·Alt+Enter and the 전체 화면 buttons. Its press is cancelled so Edge's autoscroll and
// open-in-new-window never start.
function middleDown(e){if(e.button===1)e.preventDefault();}
function middleClick(e){if(e.button!==1)return;e.preventDefault();e.stopPropagation();send('fullscreen');}
document.addEventListener('mousedown',middleDown);document.addEventListener('auxclick',middleClick);
$('image-prev').onclick=()=>stepIllustration(-1);$('image-next').onclick=()=>stepIllustration(1);
$('epub-view').addEventListener('wheel',e=>{if(e.ctrlKey||e.metaKey){fontWheel(e);return;}turnWheel(e);},{passive:false});
// 11-09: Ctrl+wheel anywhere else (the shelf, tool bars) does not zoom the whole reader page (WebView2's own zoom)
document.addEventListener('wheel',e=>{if(e.ctrlKey||e.metaKey)e.preventDefault();},{passive:false});
document.addEventListener('keydown',key);
loadShelfCovers();send('catalog');
