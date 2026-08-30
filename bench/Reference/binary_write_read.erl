#!/usr/bin/env escript
%% Read 5MB of binary, write it back, read again; print length.

main(_) ->
    {ok, F1} = file:open("bench/data/bench_binary.bin", [read, binary, raw]),
    {ok, Data} = file:read(F1, 5242880),
    ok = file:close(F1),
    ok = file:write_file("/tmp/erl_bench_binary_wr.bin", Data),
    {ok, Data2} = file:read_file("/tmp/erl_bench_binary_wr.bin"),
    io:format("~p~n", [byte_size(Data2)]).
