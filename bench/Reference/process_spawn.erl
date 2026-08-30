#!/usr/bin/env escript
%% Spawn `seq 1 10000`, read its stdout, print total byte length.

main(_) ->
    Port = erlang:open_port({spawn, "seq 1 10000"},
                            [exit_status, binary, stream]),
    Total = collect(Port, 0),
    io:format("~p~n", [Total]).

collect(Port, Acc) ->
    receive
        {Port, {data, Bin}} -> collect(Port, Acc + byte_size(Bin));
        {Port, {exit_status, _}} -> Acc
    end.
