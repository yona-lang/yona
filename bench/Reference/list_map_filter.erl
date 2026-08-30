#!/usr/bin/env escript
%% List map (*2) then filter (divisible by 4), sum.

main(_) ->
    L = build(10000, []),
    Doubled = [X * 2 || X <- L],
    Evens = [X || X <- Doubled, X rem 4 =:= 0],
    io:format("~p~n", [lists:sum(Evens)]).

build(0, Acc) -> Acc;
build(N, Acc) -> build(N - 1, [N | Acc]).
