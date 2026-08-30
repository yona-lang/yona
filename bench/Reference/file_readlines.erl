#!/usr/bin/env escript
%% Sum the character length of each line in bench_text.txt.

main(_) ->
    {ok, Bin} = file:read_file("bench/data/bench_text.txt"),
    Lines = binary:split(Bin, <<"\n">>, [global]),
    %% Yona's readLines excludes the trailing newline per line; binary:split
    %% gives us that directly. Sum character lengths (UTF-8 bytes here).
    Total = lists:foldl(fun(L, Acc) -> Acc + byte_size(L) end, 0, Lines),
    io:format("~p~n", [Total]).
