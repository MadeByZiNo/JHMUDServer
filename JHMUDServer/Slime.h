#pragma once
class User;
class Slime
{

private:
	int x;
	int y;
	int hp;
	int str;
	int dropItem;

public:
	Slime();
	void Attack(User* user);
	int SlimeDamaged(int _damage,std::string _nickname);
	int getX();
	int getY();
};