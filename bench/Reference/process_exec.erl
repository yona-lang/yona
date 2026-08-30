#!/usr/bin/env escript
%% Run 3 `echo` subprocesses in parallel, sum the output byte lengths.

main(_) ->
    Parent = self(),
    _ = [spawn_link(fun() ->
             Out = os:cmd(C),
             Parent ! {self(), length(Out)}
         end) || C <- ["echo hello", "echo world", "echo yona"]],
    Total = collect(3, 0),
    io:format("~p~n", [Total]).

collect(0, Acc) -> Acc;
collect(N, Acc) ->
    receive
        {_, Sz} -> collect(N - 1, Acc + Sz)
    end.
