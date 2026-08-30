#!/usr/bin/env escript
%% Build a set of 10K ints; report size.

main(_) ->
    S = build(10000, sets:new([{version, 2}])),
    io:format("~p~n", [sets:size(S)]).

build(0, S) -> S;
build(N, S) -> build(N - 1, sets:add_element(N, S)).
