#!/usr/bin/env escript
%% Read 50MB → write → read → print length.

main(_) ->
    {ok, Data} = file:read_file("bench/data/large_text.txt"),
    ok = file:write_file("/tmp/erl_bench_large_write.txt", Data),
    {ok, Data2} = file:read_file("/tmp/erl_bench_large_write.txt"),
    io:format("~p~n", [byte_size(Data2)]).
