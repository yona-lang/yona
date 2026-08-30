// Simple single-threaded channel simulation: producer fills an array,
// consumer sums. Matches the benchmark semantics (all work on one event
// loop) without spinning up worker threads.
(async () => {
  const buf = [];
  for (let n = 1; n <= 5000; n++) buf.push(n * 2);
  let sum = 0;
  for (const v of buf) sum += v;
  console.log(sum);
})();
