#pragma once
#include <list>
using namespace std;
class Slime;
class User
{
private:
	char nickname[100];
	int x;
	int y;
	int hp;
	int str;
	int numOfHpPotion;
	int numOfStrPotion;
public:
	User(char _nickname[]);
	User(char _nickname[],int _x,int _y,int _hp,int _str, int _numOfHpPotion,int _numOfStrPotion);
	~User();

	void PrintUserLocation();
	void Move(int _x, int _y);
	void Attack(list<Slime*> slimes);
	void UserDamaged(int _damage);
	
	const char* PrintUserInformation();

	const char* getNickname();
	int getHp();
	int getStr();
	int getNumOfHpPotion();
	int getNumOfStrPotion();
	int getX();
	int getY();
	
	

};