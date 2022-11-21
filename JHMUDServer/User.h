#pragma once
#include <list>
using namespace std;
class Slime;
class User
{
private:
	string nickname;
	int x;
	int y;
	int hp;
	int str;
	int numOfHpPotion;
	int numOfStrPotion;
public:
	User(string _nickname);
	User(string _nickname,int _x,int _y,int _hp,int _str, int _numOfHpPotion,int _numOfStrPotion);
	~User();


	bool Move(int _x, int _y);
	void Attack(list<Slime*> slimes);
	bool UserDamaged(int _damage);

	string getNickname();
	int getHp();
	int getStr();
	int getNumOfHpPotion();
	int getNumOfStrPotion();
	int getX();
	int getY();
	
	

};