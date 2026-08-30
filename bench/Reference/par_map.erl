#!/usr/bin/env escript
%% Parallel map: spawn one process per element, gather results.
%% Compute x*x*x for x in 1..20.

main(_) ->
    Self = self(),
    Pids = [spawn(fun() -> Self ! {self(), X * X * X} end) || X <- lists:seq(1, 20)],
    Result = [receive {P, V} -> V end || P <- Pids],
    Strs = [integer_to_list(N) || N <- Result],
    io:format("[~s]~n", [string:join(Strs, ", ")]).
