# Write your code here
n=int(input())
numbers=[int(x) for x in input().split()]

dp= [[0] * n for _ in range(n)]

for i in range(n):
    dp[i][i]=numbers[i]

for l in range (2,n+1):
    for i in range(n-l+1):
        j=i+l-1
        dp[i][j] = max(numbers[i]-dp[i+1][j],numbers[j]-dp[i][j-1])

total_diff=dp[0][n-1]
if total_diff>0:
    print("Player 1 wins")
elif total_diff==0:
    print("Its a draw")
else:
    print("Player 2 wins")
