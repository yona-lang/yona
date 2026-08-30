#!/usr/bin/env escript
%% Read the same file three times in parallel; sum byte lengths.
%% Uses processes + a receive loop for the join.

main(_) ->
    Parent = self(),
    _ = [spawn_link(fun() ->
             {ok, B} = file:read_file("bench/data/bench_text.txt"),
             Parent ! {self(), byte_size(B)}
         end) || _ <- [1, 2, 3]],
    Total = collect(3, 0),
    io:format("~p~n", [Total]).

collect(0, Acc) -> Acc;
collect(N, Acc) ->
    receive
        {_, Sz} -> collect(N - 1, Acc + Sz)
    end.
