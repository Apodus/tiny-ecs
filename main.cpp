
#include "ecs.hpp"

#include <iostream>
#include <chrono>

struct position {
	position() : x(0), y(0) {}
	position(float x, float y) : x(x), y(y) {}
	float x;
	float y;
	float a = 0; // angle
};

struct motion {
	motion() : dx(0), dy(0) {}
	motion(float dx, float dy) : dx(dx), dy(dy) {}
	float dx;
	float dy;
	float da = 0; // angular velocity
};



int main(int argc, char** argv) {
	urtela::ecs database;

	constexpr size_t iteration_count = 1000;
	constexpr size_t entity_count = 1000000;

	for (size_t i = 0; i < entity_count; ++i) {
		if (rand() < RAND_MAX * 1.1)
			database.create(position(static_cast<float>(rand()), static_cast<float>(rand())), motion());
		else
			database.create(position(static_cast<float>(rand()), static_cast<float>(rand())));
	}

	std::cout << database.create() << std::endl;

	auto start = std::chrono::high_resolution_clock::now();

	for (int i = 0; i < iteration_count; ++i) {
		database.for_each([](position& p, motion& m) {
			p.x += m.dx;
			p.y += m.dy;
			float sqrDistFromSun = p.x * p.x + p.y * p.y;
			float sunMass = 5734898;
			float force = sunMass / sqrDistFromSun;
			float linearDist = std::sqrt(sqrDistFromSun);
			m.dx += p.x / linearDist;
			m.dy += p.y / linearDist;
		});
	}
	auto end = std::chrono::high_resolution_clock::now();

	std::cout << ((end - start).count() / (iteration_count * 1000.0 * 1000.0)) << " milliseconds per iteration average" << std::endl;

	/*
	data.create(position(), motion());
	data.create(position(), motion());
	auto id = data.create(position());

	data.attachToEntity(id, motion());
	data.removeFromEntity<position>(id);
	*/

	/*
	data.for_each([](motion& m) { std::cout << "motion" << std::endl; });
	data.for_each([](position& m) { std::cout << "position" << std::endl; });
	data.for_each([](motion& m, position& p) { std::cout << "both" << std::endl; });
	*/
}