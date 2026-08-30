#!/usr/bin/env escript
%% Read bench_binary.bin in 64KB chunks; sum chunk byte lengths.

main(_) ->
    {ok, F} = file:open("bench/data/bench_binary.bin", [read, binary, raw]),
    Total = loop(F, 0),
    ok = file:close(F),
    io:format("~p~n", [Total]).

loop(F, Acc) ->
    case file:read(F, 65536) of
        eof -> Acc;
        {ok, Bin} -> loop(F, Acc + byte_size(Bin))
    end.
