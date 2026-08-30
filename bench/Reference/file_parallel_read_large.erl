#!/usr/bin/env escript
%% Read 4 × 10MB chunks in parallel; sum byte lengths.

main(_) ->
    Parent = self(),
    _ = [spawn_link(fun() ->
             {ok, B} = file:read_file(io_lib:format("bench/data/chunk_~p.txt", [I])),
             Parent ! {self(), byte_size(B)}
         end) || I <- [1, 2, 3, 4]],
    Total = collect(4, 0),
    io:format("~p~n", [Total]).

collect(0, Acc) -> Acc;
collect(N, Acc) ->
    receive
        {_, Sz} -> collect(N - 1, Acc + Sz)
    end.
