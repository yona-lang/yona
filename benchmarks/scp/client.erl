-module(client).
-export([client/0]).

send(Socket, File) ->
  case file:read_line(File) of
    {ok, Data} ->
      ok = gen_tcp:send(Socket, erlang:iolist_to_binary([Data])),
      io:format("~s", [Data]),
      send(Socket, File);
    eof        ->
      gen_tcp:send(Socket, <<"--over--\n">>),
      file:close(File),
      ok = gen_tcp:close(Socket)
end.

client() ->
  {ok, Socket} = gen_tcp:connect("localhost", 5555, [binary, {packet, 0}]),
  inet:setopts(Socket, [{nodelay, true}, {delay_send, false}]),

  {ok, File} = file:open("../data/big.txt", [read, binary]),
  {Time, _} = timer:tc(fun send/2, [Socket, File]),

  io:format("~p\n", [Time]).
