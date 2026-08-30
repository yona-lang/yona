#!/usr/bin/env escript
%% Fill 10K cells with 7, sum them.

main(_) ->
    A = lists:duplicate(10000, 7),
    io:format("~p~n", [lists:sum(A)]).
