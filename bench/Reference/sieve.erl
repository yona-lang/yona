#!/usr/bin/env escript
%% sieve up to 500, count primes

main(_) ->
    Primes = sieve(lists:seq(2, 500)),
    io:format("~p~n", [length(Primes)]).

sieve([]) -> [];
sieve([P | Rest]) -> [P | sieve([X || X <- Rest, X rem P =/= 0])].
