import qualified Data.Set as S
main :: IO ()
main = print (S.size (go 10000 S.empty))
  where
    go 0 s = s
    go n s = go (n - 1) (S.insert n s)
