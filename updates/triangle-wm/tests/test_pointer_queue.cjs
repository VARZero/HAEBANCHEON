// Exercise the actual browser queue with held HTTP receipts; no DOM package.
const fs = require('fs'), vm = require('vm'), assert = require('assert/strict');
const html = fs.readFileSync(require('path').join(__dirname, '../tools/pointer_monitor.html'), 'utf8');
const elements = new Map(), requests = [], intervals = [];
const context = vm.createContext({
  document: {getElementById(id) {
    if (!elements.has(id)) elements.set(id, {classList: {add(){},remove(){},toggle(){}}});
    return elements.get(id);
  }}, window: {}, navigator: {}, setTimeout(){}, setInterval(fn){intervals.push(fn);},
  fetch(url, options) {
    if (url === '/status') return Promise.resolve({json: async()=>({online:true,async_input:true,capture:{}})});
    return new Promise(resolve=>requests.push({q:JSON.parse(options.body), resolve}));
  }
});
const tick = ()=>new Promise(resolve=>setImmediate(resolve));
const send = code=>vm.runInContext(code, context);
async function reply(result, extra={accept_ms:2,present_ms:3}) {
  const r=requests.shift();r.resolve({json:async()=>({ok:true,result,...extra})});
  await tick();return r.q;
}
(async()=>{
  send(html.split('<script>')[1].split('</script>')[0]);await tick();
  assert.equal(requests[0].q.op,'input_status');await reply({display_enabled:true});
  send("send('input_move',{x:1,y:1})");
  for(let i=2;i<=20;i++)send(`send('input_move',{x:${i},y:1})`);
  send("send('input_button',{x:20,y:1,pressed:true});send('input_move',{x:21,y:1});send('input_move',{x:22,y:1})");
  assert.equal(requests.length,1,'more than one HTTP request in flight');
  await reply({accepted_ticket:1,visible_ticket:0},{accept_ms:12,completion_pending:true});
  assert.equal(requests[0].q.args.x,20,'did not replace pending movements');
  assert.match(elements.get('status').textContent,/12\.0 ms/);
  assert.match(elements.get('status').textContent,/접수 1 \/ 표시 0/,'receipt mislabeled as display completion');
  await reply({accepted_ticket:2,visible_ticket:0},{accept_ms:8,completion_pending:true});
  assert.equal(requests[0].q.op,'input_button','movement crossed button barrier');
  await reply({generation:4});
  assert.equal(requests[0].q.args.x,22);await reply({accepted_ticket:3,visible_ticket:1},{accept_ms:7});
  send("send('input_progress')");
  await reply({accepted_ticket:3,visible_ticket:3,last_accept_to_visible_us:420000},{accept_ms:99});
  assert.match(elements.get('status').textContent,/7\.0 ms/,'progress round-trip replaced movement receipt time');
  assert.match(elements.get('status').textContent,/420\.0 ms/);
  send("fastInput=true;enabled=true;acceptedTicket=9;visibleTicket=3");
  intervals.forEach(fn=>fn());assert.equal(requests.length,0,'fast mode polled progress during movement');
  send("send('input_move',{x:40,y:10})");await reply({queued_sequence:50},{queued_to_uart:true,host_queue_ms:0.1});
  assert.match(elements.get('status').textContent,/전송 예약 50/,'host queue receipt mislabeled as board acceptance');
  assert.match(elements.get('status').textContent,/0\.1 ms/);
  console.log('PASS_POINTER_QUEUE: latest movement, ordered button, early receipt, separate completion, correct timing labels');
})().catch(e=>{console.error(e);process.exitCode=1;});
