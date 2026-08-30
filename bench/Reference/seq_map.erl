#!/usr/bin/env escript
%% [x*x*x for x in 1..20]

main(_) ->
    Result = [X * X * X || X <- lists:seq(1, 20)],
    Strs = [integer_to_list(N) || N <- Result],
    io:format("[~s]~n", [string:join(Strs, ", ")]).
