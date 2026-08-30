#!/usr/bin/env escript
%% Reverse a list of 10000, return length

main(_) ->
    L = build(10000, []),
    io:format("~p~n", [length(lists:reverse(L))]).

build(0, Acc) -> Acc;
build(N, Acc) -> build(N - 1, [N | Acc]).
