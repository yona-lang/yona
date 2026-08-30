#!/usr/bin/env escript
%% tak 30 20 10

main(_) ->
    io:format("~p~n", [tak(30, 20, 10)]).

tak(X, Y, Z) when Y < X -> tak(tak(X - 1, Y, Z), tak(Y - 1, Z, X), tak(Z - 1, X, Y));
tak(_, _, Z) -> Z.
