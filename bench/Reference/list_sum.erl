#!/usr/bin/env escript
%% sum of 1..10000 built via cons

main(_) ->
    L = build(10000, []),
    io:format("~p~n", [lists:sum(L)]).

build(0, Acc) -> Acc;
build(N, Acc) -> build(N - 1, [N | Acc]).
