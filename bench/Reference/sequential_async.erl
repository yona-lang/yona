#!/usr/bin/env escript
%% Sequential 4-stage chain, each stage sleeps 100ms (= slow_identity)

main(_) ->
    A = slow_identity(100),
    B = slow_identity(A),
    C = slow_identity(B),
    D = slow_identity(C),
    io:format("~p~n", [D]).

slow_identity(X) ->
    timer:sleep(100),
    X.
