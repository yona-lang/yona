import Data.List (intercalate)
main :: IO ()
main = putStrLn ("[" ++ intercalate ", " (map (show . cube) [1..20]) ++ "]")
  where cube x = x * x * x
