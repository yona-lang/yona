function sieve(n) {
    const is_prime = new Uint8Array(n + 1).fill(1);
    is_prime[0] = is_prime[1] = 0;
    for (let i = 2; i * i <= n; i++)
        if (is_prime[i]) for (let j = i*i; j <= n; j += i) is_prime[j] = 0;
    return is_prime.reduce((a, b) => a + b, 0);
}
console.log(sieve(500));
