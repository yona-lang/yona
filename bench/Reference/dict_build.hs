import qualified Data.Map.Strict as M
main :: IO ()
main = print (M.size (go 10000 M.empty))
  where
    go 0 m = m
    go n m = go (n - 1) (M.insert n (n * n) m)
