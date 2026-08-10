const $=id=>document.getElementById(id);
let busy=false;
function show(text,ok=true){const el=$('message');el.textContent=text;el.className='toast '+(ok?'ok':'error');if(text)setTimeout(()=>{if(el.textContent===text)el.textContent='';},3500);}
function fmt(sec){sec=Math.floor(sec||0);const d=Math.floor(sec/86400);sec%=86400;const h=Math.floor(sec/3600);sec%=3600;const m=Math.floor(sec/60);const s=sec%60;return `${d}d ${String(h).padStart(2,'0')}h ${String(m).padStart(2,'0')}m ${String(s).padStart(2,'0')}s`;}
function render(d){
 const online=!!d.wifiConnected;
 $('connection').innerHTML=`<span class="status-dot"></span>${online?'STA ONLINE':'AP READY'}`;
 $('connection').className='status '+(online?'online':'offline');
 $('ip').textContent=d.ip||'—';$('apIp').textContent=d.apIp||'—';$('rssi').textContent=online?`${d.rssi} dBm`:'AP mode';
 $('heap').textContent=d.heap?`${d.heap} B`:'—';$('uptime').textContent=fmt(d.uptimeSec);$('apClients').textContent=String(d.apClients??'—');$('version').textContent=d.version||'—';
 (d.relays||[]).forEach((on,i)=>{const b=document.querySelector(`[data-toggle="${i}"]`),card=document.querySelector(`[data-index="${i}"]`);if(!b)return;b.classList.toggle('on',!!on);b.setAttribute('aria-pressed',String(!!on));const l=b.querySelector('.power-label');if(l)l.textContent=on?'ON':'OFF';if(card)card.classList.toggle('active',!!on);});
}
async function refresh(){try{const r=await fetch('/api/state',{cache:'no-store'});if(!r.ok)throw Error();render(await r.json());}catch(e){$('connection').innerHTML='<span class="status-dot"></span>DEVICE OFFLINE';$('connection').className='status offline';}}
document.querySelectorAll('[data-toggle]').forEach(button=>button.addEventListener('click',async()=>{
 if(busy)return;busy=true;button.disabled=true;const i=Number(button.dataset.toggle),current=button.classList.contains('on');
 try{const r=await fetch('/api/relay',{method:'POST',headers:{'Content-Type':'application/json'},cache:'no-store',body:JSON.stringify({index:i,on:!current})});const d=await r.json();if(!r.ok)throw Error(d.error||'Command failed');render(d);show(`${['Ceiling Fan','Charging Socket','LED 1','LED 2'][i]} ${!current?'ON':'OFF'}`);}catch(e){show(e.message,false);}finally{busy=false;button.disabled=false;}
}));
$('rebootBtn').addEventListener('click',async()=>{if(!confirm('Reboot the controller?'))return;try{await fetch('/api/reboot',{method:'POST',cache:'no-store'});}finally{show('Controller is rebooting…');}});
refresh();setInterval(refresh,2500);
