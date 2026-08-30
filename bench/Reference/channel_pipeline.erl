#!/usr/bin/env escript
%% Producer sends n*2 for n in 1..5000, consumer sums.

main(_) ->
    Self = self(),
    spawn(fun() -> producer(Self, 1) end),
    io:format("~p~n", [consume(0)]).

producer(_To, N) when N > 5000 -> ok;
producer(To, N) ->
    To ! {value, N * 2},
    producer(To, N + 1).

consume(Acc) ->
    receive
        {value, V} -> consume(Acc + V)
    after 50 ->
        Acc
    end.
