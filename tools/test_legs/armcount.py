import sys
t=sys.argv[1]; sys.argv=['x']
exec(open('pinkflip.py').read().split("for t in sys.argv[1:]")[0])
n,hits=run(t); print(t,"frames",n,"violet frames",len(hits),"strong",sum(1 for h in hits if h[1]>=100))
