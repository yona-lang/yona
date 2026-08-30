#!/usr/bin/env escript
%% fib 35

main(_) ->
    io:format("~p~n", [fib(35)]).

fib(N) when N =< 1 -> N;
fib(N) -> fib(N - 1) + fib(N - 2).
