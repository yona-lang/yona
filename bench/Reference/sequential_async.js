const slow = (x) => new Promise(r => setTimeout(() => r(x), x));
(async () => {
  const a = await slow(100);
  const b = await slow(a);
  const c = await slow(b);
  const d = await slow(c);
  console.log(d);
})();
