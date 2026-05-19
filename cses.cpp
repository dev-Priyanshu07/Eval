

#include <iostream>
#include <vector>
using namespace std;

int main(){
    long long n;
    cin >> n;
    vector<long long> a(n);
    vector<long long> pos(n + 1);  // pos[i] = position of number i
    
    for(long long i = 0; i < n; i++){
        cin >> a[i];
        pos[a[i]] = i;
    }
    
    long long rounds = 1;
    for(long long i = 2; i <= n; i++){
        if(pos[i] < pos[i - 1]){
            rounds++;
        }
    }
    
    cout << rounds << endl;
    return 0;



}
