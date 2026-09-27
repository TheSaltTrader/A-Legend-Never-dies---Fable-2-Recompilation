	uint pc = 0;
	while (true)
	{
		switch (pc)
		{
		case 0:
			p0 = r1.x == 0.0;
		case 1:
			if (p0)
			{
				pc = 3;
				continue;
			}
		case 2:
			if (p0)
			{
				r0.x = 1.0;
			}
			if (!p0)
			{
				r0.y = 1.0;
			}
		case 3:
			if (p0)
			{
				r0.z = 1.0;
			}
			return;
		}
	}
