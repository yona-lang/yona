#!/usr/bin/env escript

main(_) ->
    io:format("~p~n", [loop(50000, 0)]).

loop(0, Acc) -> Acc;
loop(N, Acc) -> loop(N - 1, Acc + 10).
