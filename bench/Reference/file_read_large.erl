#!/usr/bin/env escript
%% Read 50MB file, print byte length.

main(_) ->
    {ok, Bin} = file:read_file("bench/data/large_text.txt"),
    io:format("~p~n", [byte_size(Bin)]).
