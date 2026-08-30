#!/usr/bin/env escript
%% sum of squares 1^2 + 2^2 + ... + 1000000^2

main(_) ->
    io:format("~p~n", [loop(1000000, 0)]).

loop(N, Acc) when N =< 0 -> Acc;
loop(N, Acc) -> loop(N - 1, Acc + N * N).
