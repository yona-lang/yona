#!/usr/bin/env escript
%% Build a map of 10K int keys → squared values; report size.

main(_) ->
    D = build(10000, #{}),
    io:format("~p~n", [maps:size(D)]).

build(0, D) -> D;
build(N, D) -> build(N - 1, maps:put(N, N * N, D)).
