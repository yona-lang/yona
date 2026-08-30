#!/usr/bin/env escript
%% Read bench_text.txt, print byte length.

main(_) ->
    {ok, Bin} = file:read_file("bench/data/bench_text.txt"),
    io:format("~p~n", [byte_size(Bin)]).
