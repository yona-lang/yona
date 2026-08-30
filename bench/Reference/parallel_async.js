const slow = (x) => new Promise(r => setTimeout(() => r(x), x));
(async () => {
  const [a, b, c, d] = await Promise.all([slow(100), slow(100), slow(100), slow(100)]);
  console.log(a + b + c + d);
})();
