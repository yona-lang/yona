import asyncio


async def slow(x):
    await asyncio.sleep(x / 1000)
    return x


async def main():
    a = await slow(100)
    b = await slow(a)
    c = await slow(b)
    d = await slow(c)
    print(d)


asyncio.run(main())
