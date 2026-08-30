#!/usr/bin/env escript
%% Producer sends 1..10000, consumer sums.
%% Erlang processes have unbounded mailboxes so there's no explicit channel buffer.

main(_) ->
    Self = self(),
    spawn(fun() -> producer(Self, 1) end),
    io:format("~p~n", [consume(0)]).

producer(_To, N) when N > 10000 -> ok;
producer(To, N) ->
    To ! {value, N},
    producer(To, N + 1).

consume(Acc) ->
    receive
        {value, V} -> consume(Acc + V)
    after 50 ->
        Acc
    end.
