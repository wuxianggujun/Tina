#ifndef TINA_BASEAPPLICATION_HPP
#define TINA_BASEAPPLICATION_HPP

#include <cstdint>
#include <GLFW/glfw3.h>

namespace Tina
{
	class BaseApplication
	{
	public:
		BaseApplication(const char* title, uint32_t width, uint32_t height) :mTitle(title), mWidth(width), mHeight(height) {

		}
		virtual int run(int argc, char** argv) { return 0; };
		virtual void initialize(int argc, char** argv) {};
		virtual void shutdown() {};
		protected:
		uint32_t mWidth;
		uint32_t mHeight;
		const char* mTitle;
	};
} // Tina

#endif //TINA_BASEAPPLICATION_HPP
