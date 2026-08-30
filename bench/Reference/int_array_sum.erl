#!/usr/bin/env escript
%% Sum 1..10000.

main(_) ->
    io:format("~p~n", [lists:sum(lists:seq(1, 10000))]).
