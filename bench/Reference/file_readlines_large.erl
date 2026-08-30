#!/usr/bin/env escript
%% Sum byte length of each line in the 50MB file.

main(_) ->
    {ok, Bin} = file:read_file("bench/data/large_text.txt"),
    Lines = binary:split(Bin, <<"\n">>, [global]),
    Total = lists:foldl(fun(L, Acc) -> Acc + byte_size(L) end, 0, Lines),
    io:format("~p~n", [Total]).
