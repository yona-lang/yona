#!/usr/bin/env escript
%% Parallel: 4 sleep-100ms tasks, summed.

main(_) ->
    Self = self(),
    [spawn(fun() -> timer:sleep(100), Self ! {ok, 100} end) || _ <- lists:seq(1, 4)],
    Sum = lists:sum([receive {ok, V} -> V end || _ <- lists:seq(1, 4)]),
    io:format("~p~n", [Sum]).
