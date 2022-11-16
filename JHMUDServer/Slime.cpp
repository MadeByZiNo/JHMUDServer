#include <iostream>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include "Slime.h"	
#include "User.h"
using namespace std;



Slime::Slime()
{
	//srand((unsigned int)time(NULL));
	x = rand() % 30;
	y = rand() % 30;
	hp = rand() % 6 + 5;
	str = rand() % 3 + 3;
	dropItem = rand() % 3;
}
void Slime::Attack(User* user)
{
		int ux = user->getX();
		int uy = user->getY();
		if ((ux >= x - 1) && (ux <= x + 1) && (uy >= y - 1) && (uy <= y + 1))
		{
			std::cout << "\n\n슬라임의 공격! " << str << "의 데미지를 받았다!" << std::endl;
			user->UserDamaged(str);
		}
}
int Slime::SlimeDamaged(int _damage,string _nickname)
{
	hp -= _damage;
	std::cout << "\n\n"<<_nickname<<"의 공격! 슬라임은 " <<	_damage << "의 데미지를 받았다!" << std::endl;
	if (hp < 0)
	{
		std::cout << "\n\n슬라임을 해치웠다!" <<std::endl;
		int item = dropItem;
		delete this;
		return item;
	
	}
	return -1;
}
int Slime::getX() { return x; }
int Slime::getY() { return y; }
