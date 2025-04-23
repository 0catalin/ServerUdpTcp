def count_distinct_letters(s):
    # Convert string to lowercase to ignore case (optional, depending on your needs)
    s = s.lower()
    
    # Create a set with only alphabetic characters
    distinct_letters = {char for char in s if char.isalpha()}
    
    # Return the number of distinct letters
    return len(distinct_letters)



def count_subsequences(s, t):
    m, n = len(s), len(t)
    distinct = count_distinct_letters(t)

    # dp[i][j] = number of ways to form t[:j] using s[:i]
    dp = [[0] * (n + 1) for _ in range(m + 1)]

    occ = [0] * (m + 1)

    for i in range(1, m):
        if s[i] == '?':
            occ[i + 1] = occ[i] + 1
        else:
            occ[i + 1] = occ[i]
    print(occ)
    # Base case: empty t can be formed from any prefix of s in exactly 1 way (by deleting all characters)
    dp[0][0] = 1
    for i in range(1, m + 1):
        if s[i - 1] == '?':
            dp[i][0] = distinct * dp[i - 1][0]
        else:
            dp[i][0] = dp[i - 1][0]

    # Fill the table
    for i in range(1, m + 1):
        for j in range(1, n + 1):
            if s[i - 1] == '?':
                dp[i][j] = distinct * dp[i-1][j] + dp[i - 1][j - 1]
            elif s[i - 1] == t[j - 1]:
                # We can either match this character, or skip it
                dp[i][j] = dp[i - 1][j - 1] + dp[i - 1][j]
            else:
                # We skip s[i - 1]
                dp[i][j] = dp[i - 1][j]

    return dp



s = "ju????"
t = "juju"
  # Output: 3

def print_dp_table(dp, s, t):
    m, n = len(s), len(t)

    # Print column headers (target string t)
    header = ["    "] + ["''"] + [f"'{c}'" for c in t]
    print("   ".join(header))

    for i in range(m + 1):
        row_label = "''" if i == 0 else f"'{s[i-1]}'"
        row = [f"{row_label:>3}"] + [f"{dp[i][j]:>3}" for j in range(n + 1)]
        print("   ".join(row))

print_dp_table(count_subsequences(s, t), s , t)
