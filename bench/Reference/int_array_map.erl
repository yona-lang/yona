#!/usr/bin/env escript
%% Map (*2) over 1..10000 and sum.

main(_) ->
    Xs = lists:seq(1, 10000),
    Doubled = lists:map(fun(X) -> X * 2 end, Xs),
    io:format("~p~n", [lists:sum(Doubled)]).
