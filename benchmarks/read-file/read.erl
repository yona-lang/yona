-module(read).
-export([main/0]).

read(File) ->
  case file:read_line(File) of
    {ok, Data} -> [Data | read(File)];
    eof        -> []
  end.

main() ->
  {ok, File} = file:open("../data/big.txt", [read]),
  {Time, _} = timer:tc(fun read/1, [File]),
  io:format("~p\n", [Time]),
  file:close(File).
