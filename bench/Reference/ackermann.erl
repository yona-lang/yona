#!/usr/bin/env escript
%% Ackermann 3 11

main(_) ->
    io:format("~p~n", [ack(3, 11)]).

ack(0, N) -> N + 1;
ack(M, 0) -> ack(M - 1, 1);
ack(M, N) -> ack(M - 1, ack(M, N - 1)).
